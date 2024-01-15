#include "renderer/VulkanResourceTracker.h"

#include <unordered_map>

namespace VulkanResourceTracker
{

	struct TrackedBuffer
	{
		VkBuffer buffer = VK_NULL_HANDLE;
	};

	struct TrackedImage
	{
		VkImage image = VK_NULL_HANDLE;
		VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkPipelineStageFlags last_pipeline_stage_used = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

		uint32_t num_mips = 1;
		uint32_t num_layers = 1;
	};

	struct Data
	{
		std::unordered_map<VkBuffer, TrackedBuffer> tracked_buffers;
		std::unordered_map<VkImage, TrackedImage> tracked_images;
	} static *data;

	void Init()
	{
		data = new Data();
	}

	void Exit()
	{
	}

	void TrackBuffer(VkBuffer buffer)
	{
		auto tracked = data->tracked_buffers.find(buffer);
		VK_ASSERT(tracked == data->tracked_buffers.end() && "Tracked a buffer that was already being tracked");

		if (tracked == data->tracked_buffers.end())
		{
			data->tracked_buffers.emplace(buffer, TrackedBuffer{ .buffer = buffer });
		}
	}

	void RemoveBuffer(VkBuffer buffer)
	{
		if (data->tracked_buffers.find(buffer) == data->tracked_buffers.end())
		{
			VK_EXCEPT("VulkanResourceTracker::RemoveBuffer", "Tried to remove a buffer from being tracked that was already not tracked");
			return;
		}

		data->tracked_buffers.erase(buffer);
	}

	void TrackImage(VkImage vk_image, VkImageLayout layout, uint32_t num_mips, uint32_t num_layers)
	{
		auto tracked = data->tracked_images.find(vk_image);
		VK_ASSERT(tracked == data->tracked_images.end() && "Tracked an image that was already being tracked");

		if (tracked == data->tracked_images.end())
		{
			data->tracked_images.emplace(vk_image, TrackedImage {
					.image = vk_image,
					.layout = layout,
					.last_pipeline_stage_used = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT
				}
			);
		}
	}

	void TrackSubresource(VkImage image, VkImageLayout layout, uint32_t base_mip, uint32_t num_mips, uint32_t base_layer, uint32_t num_layers)
	{
		VK_EXCEPT("VulkanResourceTracker::TrackSubresource", "NOT IMPLEMENTED");
	}

	void RemoveImage(VkImage image)
	{
		if (data->tracked_images.find(image) == data->tracked_images.end())
		{
			VK_EXCEPT("VulkanResourceTracker::RemoveImage", "Tried to remove an image from being tracked that was already not tracked");
			return;
		}

		data->tracked_images.erase(image);
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

	VkImageMemoryBarrier2 ImageMemoryBarrier(VkImage image, VkImageLayout new_layout,
		uint32_t base_mip_level, uint32_t num_mips, uint32_t base_array_layer, uint32_t layer_count)
	{
		if (data->tracked_images.find(image) == data->tracked_images.end())
		{
			VK_EXCEPT("VulkanResourceTracker::ImageMemoryBarrier", "Tried to create an image memory barrier for an image that has not been tracked");
		}

		auto& tracked_image = data->tracked_images.at(image);

		VkImageMemoryBarrier2 image_memory_barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
		image_memory_barrier.image = tracked_image.image;
		image_memory_barrier.oldLayout = tracked_image.layout;
		image_memory_barrier.newLayout = new_layout;

		image_memory_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		image_memory_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

		image_memory_barrier.srcAccessMask = GetImageLayoutAccessFlags(tracked_image.layout);
		image_memory_barrier.srcStageMask = GetImageLayoutStageFlags(tracked_image.layout);
		image_memory_barrier.dstAccessMask = GetImageLayoutAccessFlags(new_layout);
		image_memory_barrier.dstStageMask = GetImageLayoutStageFlags(new_layout);

		if (new_layout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL || new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
		{
			image_memory_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			if (new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
			{
				image_memory_barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
			}
		}
		else
		{
			image_memory_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		}

		image_memory_barrier.subresourceRange.baseMipLevel = base_mip_level;
		image_memory_barrier.subresourceRange.levelCount = num_mips;
		image_memory_barrier.subresourceRange.baseArrayLayer = base_array_layer;
		image_memory_barrier.subresourceRange.layerCount = layer_count;

		return image_memory_barrier;
	}

	void UpdateImageLayout(VkImage image, VkImageLayout new_layout)
	{
		if (data->tracked_images.find(image) == data->tracked_images.end())
		{
			VK_EXCEPT("VulkanResourceTracker::UpdateImageLayoutAndPipeline", "Tried to transition the image layout for an image that has not been tracked");
			return;
		}

		auto& tracked_image = data->tracked_images.at(image);
		tracked_image.layout = new_layout;
		tracked_image.last_pipeline_stage_used = GetImageLayoutStageFlags(new_layout);
	}

	VkImageLayout GetImageLayout(VkImage image)
	{
		if (data->tracked_images.find(image) == data->tracked_images.end())
		{
			VK_EXCEPT("VulkanResourceTracker::GetImageLayout", "Tried to retrieve the image layout for an image that has not been tracked");
		}

		return data->tracked_images.at(image).layout;
	}

}
