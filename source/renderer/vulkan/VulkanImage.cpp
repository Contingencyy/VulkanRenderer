#include "Precomp.h"
#include "renderer/vulkan/VulkanImage.h"
#include "renderer/vulkan/VulkanInstance.h"
#include "renderer/vulkan/VulkanAllocator.h"
#include "renderer/vulkan/VulkanResourceTracker.h"
#include "renderer/vulkan/VulkanUtils.h"

namespace Vulkan
{

	VulkanImage CreateImage(const TextureCreateInfo& texture_info)
	{
		VkImageCreateInfo vk_image_info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		vk_image_info.imageType = VK_IMAGE_TYPE_2D;
		vk_image_info.extent.width = texture_info.width;
		vk_image_info.extent.height = texture_info.height;
		vk_image_info.extent.depth = 1;
		vk_image_info.mipLevels = texture_info.num_mips;
		vk_image_info.arrayLayers = texture_info.num_layers;
		vk_image_info.format = Util::ToVkFormat(texture_info.format);
		vk_image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
		vk_image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		vk_image_info.usage = Util::ToVkImageUsageFlags(texture_info.usage_flags);
		vk_image_info.samples = VK_SAMPLE_COUNT_1_BIT;
		vk_image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		vk_image_info.flags = texture_info.dimension == TEXTURE_DIMENSION_CUBE ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;

		VkImage vk_image = VK_NULL_HANDLE;
		VkCheckResult(vkCreateImage(vk_inst.device, &vk_image_info, nullptr, &vk_image));
		Vulkan::DebugNameObject((uint64_t)vk_image, VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT, texture_info.name.c_str());

		VulkanImage image = {};
		image.vk_image = vk_image;
		image.vk_format = vk_image_info.format;
		image.width = vk_image_info.extent.width;
		image.height = vk_image_info.extent.height;
		image.depth = vk_image_info.extent.depth;
		image.num_mips = vk_image_info.mipLevels;
		image.num_layers = vk_image_info.arrayLayers;
		image.memory = AllocateDeviceMemory(image, texture_info);

		ResourceTracker::TrackImage(image, vk_image_info.initialLayout);

		return image;
	}

	void DestroyImage(VulkanImage& image)
	{
		vkDestroyImage(vk_inst.device, image.vk_image, nullptr);
	}

	void CopyFromBuffer(VulkanCommandBuffer& command_buffer, VulkanBuffer& src_buffer, uint64_t src_offset, VulkanImage& dst_image, uint32_t dst_width, uint32_t dst_height)
	{
		VkBufferImageCopy2 buffer_image_copy = { VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2 };
		buffer_image_copy.bufferOffset = src_offset;
		buffer_image_copy.bufferImageHeight = 0;
		buffer_image_copy.bufferRowLength = 0;

		buffer_image_copy.imageExtent = { dst_width, dst_height, 1 };
		buffer_image_copy.imageOffset = { 0, 0, 0 };

		buffer_image_copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		buffer_image_copy.imageSubresource.mipLevel = 0;
		buffer_image_copy.imageSubresource.baseArrayLayer = 0;
		buffer_image_copy.imageSubresource.layerCount = 1;

		VkCopyBufferToImageInfo2 copy_buffer_image_info = { VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2 };
		copy_buffer_image_info.srcBuffer = src_buffer.vk_buffer;
		copy_buffer_image_info.dstImage = dst_image.vk_image;
		copy_buffer_image_info.dstImageLayout = ResourceTracker::GetImageLayout({ dst_image.vk_image });
		copy_buffer_image_info.regionCount = 1;
		copy_buffer_image_info.pRegions = &buffer_image_copy;

		vkCmdCopyBufferToImage2(command_buffer.vk_command_buffer, &copy_buffer_image_info);
	}

	void GenerateMips(VulkanCommandBuffer& command_buffer, VulkanImage& image)
	{
		VkFormatProperties format_properties;
		vkGetPhysicalDeviceFormatProperties(vk_inst.physical_device, image.vk_format, &format_properties);

		if (!(format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
		{
			VK_EXCEPT("Vulkan", "Texture image format does not support linear filter in blitting operation");
		}

		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.image = image.vk_image;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.subresourceRange.levelCount = 1;

		int32_t mip_width = image.width;
		int32_t mip_height = image.height;

		for (uint32_t i = 1; i < image.num_mips; ++i)
		{
			barrier.subresourceRange.baseMipLevel = i - 1;
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

			vkCmdPipelineBarrier(command_buffer.vk_command_buffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
				0, nullptr,
				0, nullptr,
				1, &barrier
			);

			VkImageBlit blit = {};
			blit.srcOffsets[0] = { 0, 0, 0 };
			blit.srcOffsets[1] = { mip_width, mip_height, 1 };
			blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.srcSubresource.mipLevel = i - 1;
			blit.srcSubresource.baseArrayLayer = 0;
			blit.srcSubresource.layerCount = 1;
			blit.dstOffsets[0] = { 0, 0, 0 };
			blit.dstOffsets[1] = { mip_width > 1 ? mip_width / 2 : 1, mip_height > 1 ? mip_height / 2 : 1, 1 };
			blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.dstSubresource.mipLevel = i;
			blit.dstSubresource.baseArrayLayer = 0;
			blit.dstSubresource.layerCount = 1;

			vkCmdBlitImage(command_buffer.vk_command_buffer,
				image.vk_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				image.vk_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1, &blit,
				VK_FILTER_LINEAR
			);

			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			vkCmdPipelineBarrier(command_buffer.vk_command_buffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
				0, nullptr,
				0, nullptr,
				1, &barrier
			);

			if (mip_width > 1) mip_width /= 2;
			if (mip_height > 1) mip_height /= 2;
		}

		barrier.subresourceRange.baseMipLevel = image.num_mips - 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier(command_buffer.vk_command_buffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
			0, nullptr,
			0, nullptr,
			1, &barrier
		);

		VulkanImageLayoutTransition layout_info = {};
		layout_info.image = image;
		layout_info.new_layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
		ResourceTracker::UpdateImageLayout(layout_info);
	}

	void TransitionImageLayout(VulkanCommandBuffer& command_buffer, const VulkanImageLayoutTransition& layout_info)
	{
		TransitionImageLayouts(command_buffer, { layout_info });
	}

	void TransitionImageLayouts(VulkanCommandBuffer& command_buffer, const std::vector<VulkanImageLayoutTransition> layout_infos)
	{
		std::vector<VkImageMemoryBarrier2> image_barriers;
		for (auto& transition : layout_infos)
		{
			image_barriers.push_back(ResourceTracker::ImageMemoryBarrier(transition));
			ResourceTracker::UpdateImageLayout(transition);
		}

		VkDependencyInfo dependency_info = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		dependency_info.imageMemoryBarrierCount = static_cast<uint32_t>(image_barriers.size());
		dependency_info.pImageMemoryBarriers = image_barriers.data();
		dependency_info.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		vkCmdPipelineBarrier2(command_buffer.vk_command_buffer, &dependency_info);
	}

	uint64_t GetImageAlign(VulkanImage image)
	{
		VkMemoryRequirements2 memory_req = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 };
		VkImageMemoryRequirementsInfo2 image_memory_req = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2 };
		image_memory_req.image = image.vk_image;

		vkGetImageMemoryRequirements2(vk_inst.device, &image_memory_req, &memory_req);
		return memory_req.memoryRequirements.alignment;
	}

}
