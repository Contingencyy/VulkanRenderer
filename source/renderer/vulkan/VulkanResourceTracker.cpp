#include "Precomp.h"
#include "renderer/vulkan/VulkanResourceTracker.h"
#include "renderer/vulkan/VulkanSync.h"
#include "renderer/vulkan/VulkanUtils.h"

namespace Vulkan
{

	namespace ResourceTracker
	{

		struct TrackedBuffer
		{
			VulkanBuffer buffer;
			VkAccessFlags2 last_access_flags = VK_ACCESS_2_NONE;
			VkPipelineStageFlagBits2 last_stage_flags = VK_PIPELINE_STAGE_2_NONE;

			VulkanFence fence;
		};

		struct TrackedImage
		{
			VulkanImage image;
			VkAccessFlags2 last_access_flags = VK_ACCESS_2_NONE;
			VkPipelineStageFlags2 last_stage_flags = VK_PIPELINE_STAGE_2_NONE;

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

		VkBufferMemoryBarrier2 BufferMemoryBarrier(const VulkanBufferBarrier& barrier)
		{
			VkBufferMemoryBarrier2 memory_barrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
			memory_barrier.buffer = barrier.buffer.vk_buffer;
			memory_barrier.size = barrier.buffer.size_in_bytes;
			memory_barrier.offset = barrier.buffer.offset_in_bytes;

			memory_barrier.srcAccessMask = barrier.src_access_flags;
			memory_barrier.srcStageMask = barrier.src_stage_flags;
			memory_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

			memory_barrier.dstAccessMask = barrier.dst_access_flags;
			memory_barrier.dstStageMask = barrier.dst_stage_flags;
			memory_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

			return memory_barrier;
		}

		void UpdateBufferAccessAndStageFlags(const VulkanBufferBarrier& barrier)
		{
			auto tracked = data->tracked_buffers.find(barrier.buffer.vk_buffer);
			VK_ASSERT(tracked != data->tracked_buffers.end() && "Tried to update access and stage flags for a buffer that has not been tracked");

			tracked->second.last_access_flags = barrier.dst_access_flags;
			tracked->second.last_stage_flags = barrier.dst_stage_flags;
		}

		void TrackImage(const VulkanImage& image, VkImageLayout layout)
		{
			auto tracked = data->tracked_images.find(image.vk_image);
			VK_ASSERT(tracked == data->tracked_images.end() && "Tracked an image that was already being tracked");

			if (tracked == data->tracked_images.end())
			{
				data->tracked_images.emplace(image.vk_image, TrackedImage{
						.image = image,
						.last_stage_flags = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
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

		VkImageMemoryBarrier2 ImageMemoryBarrier(const VulkanImageBarrier& barrier)
		{
			VK_ASSERT(data->tracked_images.find(barrier.image.vk_image) != data->tracked_images.end() && "Tried to create an image memory barrier for an image that has not been tracked");
			auto& tracked_image = data->tracked_images.at(barrier.image.vk_image);

			uint32_t num_mips = barrier.num_mips == UINT32_MAX ? tracked_image.image.num_mips : barrier.num_mips;
			uint32_t num_layers = barrier.num_layers == UINT32_MAX ? tracked_image.image.num_layers : barrier.num_layers;

			VkImageMemoryBarrier2 image_memory_barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
			image_memory_barrier.image = tracked_image.image.vk_image;
			image_memory_barrier.oldLayout = tracked_image.layout;
			image_memory_barrier.newLayout = barrier.new_layout;

			image_memory_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			image_memory_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

			image_memory_barrier.srcAccessMask = tracked_image.last_access_flags;
			image_memory_barrier.srcStageMask = tracked_image.last_stage_flags;
			image_memory_barrier.dstAccessMask = Util::GetAccessFlagsFromImageLayout(barrier.new_layout);
			image_memory_barrier.dstStageMask = Util::GetPipelineStageFlagsFromImageLayout(barrier.new_layout);

			if (barrier.new_layout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL || barrier.new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
			{
				image_memory_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
				if (barrier.new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
				{
					image_memory_barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
				}
			}
			else
			{
				image_memory_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			}

			image_memory_barrier.subresourceRange.baseMipLevel = barrier.base_mip;
			image_memory_barrier.subresourceRange.levelCount = num_mips;
			image_memory_barrier.subresourceRange.baseArrayLayer = barrier.base_layer;
			image_memory_barrier.subresourceRange.layerCount = num_layers;

			return image_memory_barrier;
		}

		void UpdateImageAccessAndStageFlags(const VulkanImageBarrier& barrier)
		{
			VK_ASSERT(data->tracked_images.find(barrier.image.vk_image) != data->tracked_images.end() && "Tried to create an image memory barrier for an image that has not been tracked");
			auto& tracked_image = data->tracked_images.at(barrier.image.vk_image);

			uint32_t num_mips = barrier.num_mips == UINT32_MAX ? tracked_image.image.num_mips : barrier.num_mips;
			uint32_t num_layers = barrier.num_layers == UINT32_MAX ? tracked_image.image.num_layers : barrier.num_layers;

			tracked_image.last_access_flags = Util::GetAccessFlagsFromImageLayout(barrier.new_layout);
			tracked_image.last_stage_flags = Util::GetPipelineStageFlagsFromImageLayout(barrier.new_layout);

			if (barrier.base_mip != 0 || barrier.base_layer != 0 || num_mips != tracked_image.image.num_mips || num_layers != tracked_image.image.num_layers)
			{
				// NOTE: For most images, we do not need fine grained image layouts for individual mips and/or layers, so we only do this when we explicitly
				// start tracking a subresource within an already tracked image
				if (tracked_image.subresource_layouts.size() == 0)
					tracked_image.subresource_layouts.resize(tracked_image.image.num_mips * tracked_image.image.num_layers);

				for (uint32_t mip = barrier.base_mip; mip < barrier.base_mip + num_mips; ++mip)
				{
					for (uint32_t layer = barrier.base_layer; layer < barrier.base_layer + num_layers; ++layer)
					{
						uint32_t subresource_index = tracked_image.image.num_mips * layer + mip;
						tracked_image.subresource_layouts[subresource_index] = barrier.new_layout;
					}
				}

				tracked_image.layout = VK_IMAGE_LAYOUT_UNDEFINED;
			}
			else
			{
				tracked_image.layout = barrier.new_layout;
			}
		}

		VkImageLayout GetImageLayout(const VulkanImageBarrier& barrier)
		{
			VK_ASSERT(data->tracked_images.find(barrier.image.vk_image) != data->tracked_images.end() && "Tried to retrieve the image layout for an image that has not been tracked");
			auto& tracked_image = data->tracked_images.at(barrier.image.vk_image);

			uint32_t num_mips = barrier.num_mips == UINT32_MAX ? tracked_image.image.num_mips : barrier.num_mips;
			uint32_t num_layers = barrier.num_layers == UINT32_MAX ? tracked_image.image.num_layers : barrier.num_layers;

			if (barrier.base_mip != 0 || barrier.base_layer != 0 || num_mips != tracked_image.image.num_mips || num_layers != tracked_image.image.num_layers)
			{
				uint32_t subresource_index = barrier.base_layer * tracked_image.image.num_mips + barrier.base_mip;
				VkImageLayout layout = tracked_image.subresource_layouts[subresource_index];
				bool layout_changes_in_range = false;

				for (uint32_t mip = barrier.base_mip; mip < barrier.base_mip + num_mips; ++mip)
				{
					for (uint32_t layer = barrier.base_layer; layer < barrier.base_layer + num_layers; ++layer)
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
			for (auto it = data->tracked_buffers.begin(); it != data->tracked_buffers.end(); ++it)
			{
				if (it->second.fence.vk_semaphore &&
					it->second.fence.fence_value <= Vulkan::Sync::GetFenceValue(it->second.fence))
				{
					it = data->tracked_buffers.erase(it);
				}
			}

			for (auto it = data->tracked_images.begin(); it != data->tracked_images.end(); ++it)
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
