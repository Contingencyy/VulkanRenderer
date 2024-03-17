#include "Precomp.h"
#include "renderer/vulkan/VulkanResourceTracker.h"
#include "renderer/vulkan/VulkanSync.h"

namespace Vulkan
{

	namespace ResourceTracker
	{

		struct TrackedBuffer
		{
			VulkanBuffer buffer;

			VulkanFence fence;
		};

		struct TrackedImage
		{
			VulkanImage image;
			VkPipelineStageFlags last_pipeline_stage_used = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

			VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
			std::vector<VkImageLayout> subresource_layouts;

			VulkanFence fence;
		};

		struct Data
		{
			std::unordered_map<VkBuffer, TrackedBuffer> tracked_buffers;
			std::unordered_map<VkImage, TrackedImage> tracked_images;
		} static* data;

		void Init()
		{
			data = new Data();
		}

		void Exit()
		{
			delete data;
		}

		void TrackBuffer(const VulkanBuffer& buffer)
		{
			auto tracked = data->tracked_buffers.find(buffer.vk_buffer);
			VK_ASSERT(tracked == data->tracked_buffers.end() && "Tracked a buffer that was already being tracked");

			if (tracked == data->tracked_buffers.end())
			{
				data->tracked_buffers.emplace(buffer.vk_buffer, TrackedBuffer{ .buffer = buffer });
			}
		}

		void TrackBufferTemp(const VulkanBuffer& buffer, const VulkanFence& fence, uint64_t fence_value)
		{
			auto tracked = data->tracked_buffers.find(buffer.vk_buffer);
			VK_ASSERT(tracked != data->tracked_buffers.end() && "Tried to add temporary tracking to a buffer that was not previously tracked");

			tracked->second.fence = fence;
			tracked->second.fence.fence_value = fence_value;
		}

		void RemoveBuffer(const VulkanBuffer& buffer)
		{
			VK_ASSERT(data->tracked_buffers.find(buffer.vk_buffer) != data->tracked_buffers.end() &&
				"Tried to remove a buffer from being tracked that was already not tracked");

			data->tracked_buffers.erase(buffer.vk_buffer);
		}

		void TrackImage(const VulkanImage& image, VkImageLayout layout)
		{
			auto tracked = data->tracked_images.find(image.vk_image);
			VK_ASSERT(tracked == data->tracked_images.end() && "Tracked an image that was already being tracked");

			if (tracked == data->tracked_images.end())
			{
				data->tracked_images.emplace(image.vk_image, TrackedImage{
						.image = image,
						.last_pipeline_stage_used = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
						.layout = layout
					}
				);
			}
		}

		void TrackImageTemp(const VulkanImage& image, const VulkanFence& fence, uint64_t fence_value)
		{
			auto tracked = data->tracked_images.find(image.vk_image);
			VK_ASSERT(tracked != data->tracked_images.end() && "Tried to add temporary tracking to an image that was not previously tracked");
			
			tracked->second.fence = fence;
			tracked->second.fence.fence_value = fence_value;
		}

		void RemoveImage(const VulkanImage& image)
		{
			VK_ASSERT(data->tracked_images.find(image.vk_image) != data->tracked_images.end() &&
				"Tried to remove an image from being tracked that was already not tracked");

			data->tracked_images.erase(image.vk_image);
		}

		static VkAccessFlags2 GetImageLayoutAccessFlags(VkImageLayout layout)
		{
			VkAccessFlags2 access_flags = 0;

			switch (layout)
			{
			case VK_IMAGE_LAYOUT_UNDEFINED:
			case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
				access_flags = VK_ACCESS_2_NONE;
				break;
			case VK_IMAGE_LAYOUT_GENERAL:
				access_flags = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
				break;
			case VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL:
			case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
				access_flags = VK_ACCESS_2_SHADER_READ_BIT;
				break;
			case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
				access_flags = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
				break;
			case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
			case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
				access_flags = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
				break;
			case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
				access_flags = VK_ACCESS_2_TRANSFER_READ_BIT;
				break;
			case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
				access_flags = VK_ACCESS_2_TRANSFER_WRITE_BIT;
				break;
			case VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL:
				access_flags = VK_ACCESS_2_SHADER_WRITE_BIT;
				break;
			case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
				access_flags = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
				break;
			default:
				VK_EXCEPT("Vulkan", "No VkAccessFlags2 found for layout");
				break;
			}

			return access_flags;
		}

		static VkPipelineStageFlags2 GetImageLayoutStageFlags(VkImageLayout layout)
		{
			VkPipelineStageFlags2 stage_flags = 0;

			switch (layout)
			{
			case VK_IMAGE_LAYOUT_UNDEFINED:
				stage_flags = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
				break;
			case VK_IMAGE_LAYOUT_GENERAL:
			case VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL:
			case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
				stage_flags = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
				break;
			case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
				stage_flags = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
				break;
			case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
				stage_flags = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
				break;
			case VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL:
				stage_flags = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
				break;
			case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
			case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
			case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
				stage_flags = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
				break;
			case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
				stage_flags = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
				break;
			default:
				VK_EXCEPT("Vulkan", "No VkPipelineStageFlags2 found for layout");
				break;
			}

			return stage_flags;
		}

		VkImageMemoryBarrier2 ImageMemoryBarrier(const VulkanImageLayoutTransition& layout_info)
		{
			VK_ASSERT(data->tracked_images.find(layout_info.image.vk_image) != data->tracked_images.end() && "Tried to create an image memory barrier for an image that has not been tracked");
			auto& tracked_image = data->tracked_images.at(layout_info.image.vk_image);

			uint32_t num_mips = layout_info.num_mips == UINT32_MAX ? tracked_image.image.num_mips : layout_info.num_mips;
			uint32_t num_layers = layout_info.num_layers == UINT32_MAX ? tracked_image.image.num_layers : layout_info.num_layers;

			VkImageMemoryBarrier2 image_memory_barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
			image_memory_barrier.image = tracked_image.image.vk_image;
			image_memory_barrier.oldLayout = tracked_image.layout;
			image_memory_barrier.newLayout = layout_info.new_layout;

			image_memory_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			image_memory_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

			image_memory_barrier.srcAccessMask = GetImageLayoutAccessFlags(tracked_image.layout);
			image_memory_barrier.srcStageMask = GetImageLayoutStageFlags(tracked_image.layout);
			image_memory_barrier.dstAccessMask = GetImageLayoutAccessFlags(layout_info.new_layout);
			image_memory_barrier.dstStageMask = GetImageLayoutStageFlags(layout_info.new_layout);

			if (layout_info.new_layout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL || layout_info.new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
			{
				image_memory_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
				if (layout_info.new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
				{
					image_memory_barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
				}
			}
			else
			{
				image_memory_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			}

			image_memory_barrier.subresourceRange.baseMipLevel = layout_info.base_mip;
			image_memory_barrier.subresourceRange.levelCount = num_mips;
			image_memory_barrier.subresourceRange.baseArrayLayer = layout_info.base_layer;
			image_memory_barrier.subresourceRange.layerCount = num_layers;

			return image_memory_barrier;
		}

		void UpdateImageLayout(const VulkanImageLayoutTransition& layout_info)
		{
			VK_ASSERT(data->tracked_images.find(layout_info.image.vk_image) != data->tracked_images.end() && "Tried to create an image memory barrier for an image that has not been tracked");
			auto& tracked_image = data->tracked_images.at(layout_info.image.vk_image);

			uint32_t num_mips = layout_info.num_mips == UINT32_MAX ? tracked_image.image.num_mips : layout_info.num_mips;
			uint32_t num_layers = layout_info.num_layers == UINT32_MAX ? tracked_image.image.num_layers : layout_info.num_layers;

			tracked_image.last_pipeline_stage_used = GetImageLayoutStageFlags(layout_info.new_layout);

			if (layout_info.base_mip != 0 || layout_info.base_layer != 0 || num_mips != tracked_image.image.num_mips || num_layers != tracked_image.image.num_layers)
			{
				// NOTE: For most images, we do not need fine grained image layouts for individual mips and/or layers, so we only do this when we explicitly
				// start tracking a subresource within an already tracked image
				if (tracked_image.subresource_layouts.size() == 0)
					tracked_image.subresource_layouts.resize(tracked_image.image.num_mips * tracked_image.image.num_layers);

				for (uint32_t mip = layout_info.base_mip; mip < layout_info.base_mip + num_mips; ++mip)
				{
					for (uint32_t layer = layout_info.base_layer; layer < layout_info.base_layer + num_layers; ++layer)
					{
						uint32_t subresource_index = tracked_image.image.num_mips * layer + mip;
						tracked_image.subresource_layouts[subresource_index] = layout_info.new_layout;
					}
				}

				tracked_image.layout = VK_IMAGE_LAYOUT_UNDEFINED;
			}
			else
			{
				tracked_image.layout = layout_info.new_layout;
			}
		}

		VkImageLayout GetImageLayout(const VulkanImageLayoutTransition& layout_info)
		{
			VK_ASSERT(data->tracked_images.find(layout_info.image.vk_image) != data->tracked_images.end() && "Tried to retrieve the image layout for an image that has not been tracked");
			auto& tracked_image = data->tracked_images.at(layout_info.image.vk_image);

			uint32_t num_mips = layout_info.num_mips == UINT32_MAX ? tracked_image.image.num_mips : layout_info.num_mips;
			uint32_t num_layers = layout_info.num_layers == UINT32_MAX ? tracked_image.image.num_layers : layout_info.num_layers;

			if (layout_info.base_mip != 0 || layout_info.base_layer != 0 || num_mips != tracked_image.image.num_mips || num_layers != tracked_image.image.num_layers)
			{
				uint32_t subresource_index = layout_info.base_layer * tracked_image.image.num_mips + layout_info.base_mip;
				VkImageLayout layout = tracked_image.subresource_layouts[subresource_index];
				bool layout_changes_in_range = false;

				for (uint32_t mip = layout_info.base_mip; mip < layout_info.base_mip + num_mips; ++mip)
				{
					for (uint32_t layer = layout_info.base_layer; layer < layout_info.base_layer + num_layers; ++layer)
					{
						uint32_t subresource_index = tracked_image.image.num_mips * layer + mip;

						if (layout != tracked_image.subresource_layouts[subresource_index])
						{
							layout_changes_in_range = true;
							break;
						}
					}
				}

				VK_ASSERT(!layout_changes_in_range && "Tried to get the image layout for a specific subresource range, but the subresource range specified is in multiple layouts");
				return layout;
			}

			return tracked_image.layout;
		}

		void ReleaseStaleTempResources()
		{
			for (auto it = data->tracked_buffers.begin(); it != data->tracked_buffers.end();)
			{
				if (it->second.fence.vk_semaphore &&
					it->second.fence.fence_value <= Vulkan::Sync::GetFenceValue(it->second.fence))
				{
					it = data->tracked_buffers.erase(it);
				}
			}

			for (auto it = data->tracked_images.begin(); it != data->tracked_images.end();)
			{
				if (it->second.fence.vk_semaphore &&
					it->second.fence.fence_value <= Vulkan::Sync::GetFenceValue(it->second.fence))
				{
					it = data->tracked_images.erase(it);
				}
			}
		}

	}

}
