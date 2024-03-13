#include "Precomp.h"
#include "renderer/vulkan/VulkanCommands.h"
#include "renderer/vulkan/VulkanCommandQueue.h"
#include "renderer/vulkan/VulkanCommandBuffer.h"
#include "renderer/vulkan/VulkanSync.h"
#include "renderer/vulkan/VulkanBackend.h"
#include "renderer/vulkan/VulkanResourceTracker.h"
#include "renderer/vulkan/VulkanInstance.h"

namespace Vulkan
{

	VulkanCommandBuffer GetCommandBuffer(VulkanCommandBufferType type)
	{
		VulkanCommandBuffer command_buffer;
		switch (command_buffer.type)
		{
		case VULKAN_COMMAND_BUFFER_TYPE_GRAPHICS_COMPUTE:
			command_buffer = CommandBuffer::Allocate(vk_inst.command_pools.graphics_compute);
			break;
		case VULKAN_COMMAND_BUFFER_TYPE_TRANSFER:
			command_buffer = CommandBuffer::Allocate(vk_inst.command_pools.transfer);
			break;
		default:
			VK_EXCEPT("VulkanCommands::BeginRecording", "Tried to begin recording on a command buffer type that is invalid");
			break;
		}

		return command_buffer;
	}

	void FreeCommandBuffer(VulkanCommandBuffer& command_buffer)
	{
		VulkanCommandPool command_pool;
		switch (command_buffer.type)
		{
		case VULKAN_COMMAND_BUFFER_TYPE_GRAPHICS_COMPUTE:
			command_pool = vk_inst.command_pools.graphics_compute;
			break;
		case VULKAN_COMMAND_BUFFER_TYPE_TRANSFER:
			command_pool = vk_inst.command_pools.transfer;
			break;
		default:
			VK_EXCEPT("VulkanCommands::BeginRecording", "Tried to begin recording on a command buffer type that is invalid");
			break;
		}

		CommandBuffer::Free(command_pool, command_buffer);
	}

	namespace Command
	{

		static VkIndexType ToVkIndexType(uint32_t index_byte_size)
		{
			switch (index_byte_size)
			{
			case 2:
				return VK_INDEX_TYPE_UINT16;
			case 4:
				return VK_INDEX_TYPE_UINT32;
			default:
				VK_EXCEPT("Vulkan::Command::ToVkIndexType", "Index byte size is not supported");
			}
		}

		void SetViewport(const VulkanCommandBuffer& command_buffer, uint32_t first_viewport, uint32_t num_viewports, const VkViewport* const vk_viewports)
		{
			vkCmdSetViewport(command_buffer.vk_command_buffer, first_viewport, num_viewports, vk_viewports);
		}

		void SetScissor(const VulkanCommandBuffer& command_buffer, uint32_t first_scissor, uint32_t num_scissors, const VkRect2D* const vk_scissor_rects)
		{
			vkCmdSetScissor(command_buffer.vk_command_buffer, first_scissor, num_scissors, vk_scissor_rects);
		}

		void DrawGeometry(const VulkanCommandBuffer& command_buffer, uint32_t num_vertex_buffers, const VulkanBuffer* vertex_buffers,
			uint32_t num_vertices, uint32_t num_instances, uint32_t first_vertex, uint32_t first_instance)
		{
			auto descriptor_buffer_binding_info = Vulkan::GetDescriptorBufferBindingInfos();
			vk_inst.pFunc.cmd_bind_descriptor_buffers_ext(command_buffer.vk_command_buffer, (uint32_t)descriptor_buffer_binding_info.size(), descriptor_buffer_binding_info.data());
			vk_inst.pFunc.cmd_set_descriptor_buffer_offsets_ext(command_buffer.vk_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline_layout, first, count, indices, offsets);

			std::vector<VkBuffer> vk_vertex_buffers;
			std::vector<uint64_t> vk_vertex_buffer_offsets;

			for (uint32_t i = 0; i < num_vertex_buffers; ++i)
			{
				vk_vertex_buffers.push_back(vertex_buffers[i].vk_buffer);
				vk_vertex_buffer_offsets.push_back(vertex_buffers[i].offset_in_bytes);
			}

			vkCmdBindVertexBuffers(command_buffer.vk_command_buffer, 0, 1, vk_vertex_buffers.data(), vk_vertex_buffer_offsets.data());
			vkCmdDraw(command_buffer.vk_command_buffer, num_vertices, num_instances, first_vertex, first_instance);
		}

		void DrawGeometryIndexed(const VulkanCommandBuffer& command_buffer, uint32_t num_vertex_buffers, const VulkanBuffer* const vertex_buffers, const VulkanBuffer* const index_buffer,
			uint32_t index_byte_size, uint32_t num_instances, uint32_t first_instance, uint32_t first_index, uint32_t vertex_offset)
		{
			auto descriptor_buffer_binding_info = Vulkan::GetDescriptorBufferBindingInfos();
			vk_inst.pFunc.cmd_bind_descriptor_buffers_ext(command_buffer.vk_command_buffer, (uint32_t)descriptor_buffer_binding_info.size(), descriptor_buffer_binding_info.data());
			vk_inst.pFunc.cmd_set_descriptor_buffer_offsets_ext(command_buffer.vk_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline_layout, first, count, indices, offsets);

			std::vector<VkBuffer> vk_vertex_buffers;
			std::vector<uint64_t> vk_vertex_buffer_offsets;

			for (uint32_t i = 0; i < num_vertex_buffers; ++i)
			{
				vk_vertex_buffers.push_back(vertex_buffers[i].vk_buffer);
				vk_vertex_buffer_offsets.push_back(vertex_buffers[i].offset_in_bytes);
			}

			vkCmdBindVertexBuffers(command_buffer.vk_command_buffer, 0, 1, vk_vertex_buffers.data(), vk_vertex_buffer_offsets.data() );
			vkCmdBindIndexBuffer(command_buffer.vk_command_buffer, index_buffer->vk_buffer, index_buffer->offset_in_bytes, ToVkIndexType(index_byte_size));
			vkCmdDrawIndexed(command_buffer.vk_command_buffer, index_buffer->size_in_bytes / index_byte_size, num_instances, first_index, vertex_offset, first_instance);
		}

		void Dispatch(const VulkanCommandBuffer& command_buffer, uint32_t group_x, uint32_t group_y, uint32_t group_z)
		{
			auto descriptor_buffer_binding_info = Vulkan::GetDescriptorBufferBindingInfos();
			vk_inst.pFunc.cmd_bind_descriptor_buffers_ext(command_buffer.vk_command_buffer, (uint32_t)descriptor_buffer_binding_info.size(), descriptor_buffer_binding_info.data());
			vk_inst.pFunc.cmd_set_descriptor_buffer_offsets_ext(command_buffer.vk_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline_layout, first, count, indices, offsets);

			vkCmdDispatch(command_buffer.vk_command_buffer, group_x, group_y, group_z);
		}

		void CopyBuffers(const VulkanCommandBuffer& command_buffer, const VulkanBuffer& src_buffer, uint64_t src_offset, const VulkanBuffer& dst_buffer, uint64_t dst_offset, uint64_t num_bytes)
		{
			VkBufferCopy copy_region = {};
			copy_region.srcOffset = src_offset;
			copy_region.dstOffset = dst_offset;
			copy_region.size = num_bytes;

			vkCmdCopyBuffer(command_buffer.vk_command_buffer, src_buffer.vk_buffer, dst_buffer.vk_buffer, 1, &copy_region);
		}

		void CopyFromBuffer(const VulkanCommandBuffer& command_buffer, const VulkanBuffer& src_buffer, uint64_t src_offset, const VulkanImage& dst_image, uint32_t dst_width, uint32_t dst_height)
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

		void CopyImages(const VulkanCommandBuffer& command_buffer, const VulkanImage& src_image, const VulkanImage& dst_image)
		{
			// We use vkCmdBlitImage here to have format conversions done automatically for us
			// E.g. R8G8B8A8 to B8G8R8A8
			VulkanImage swapchain_image = vk_inst.swapchain.images[vk_inst.swapchain.current_image];

			VkImageBlit blit_region = {};
			blit_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit_region.srcSubresource.mipLevel = 0;
			blit_region.srcSubresource.baseArrayLayer = 0;
			blit_region.srcSubresource.layerCount = 1;
			blit_region.srcOffsets[0] = { 0, 0, 0 };
			blit_region.srcOffsets[1] = { (int32_t)vk_inst.swapchain.extent.width, (int32_t)vk_inst.swapchain.extent.height, 1 };

			blit_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit_region.dstSubresource.mipLevel = 0;
			blit_region.dstSubresource.baseArrayLayer = 0;
			blit_region.dstSubresource.layerCount = 1;
			blit_region.dstOffsets[0] = { 0, 0, 0 };
			blit_region.dstOffsets[1] = { (int32_t)vk_inst.swapchain.extent.width, (int32_t)vk_inst.swapchain.extent.height, 1 };

			vkCmdBlitImage(
				command_buffer.vk_command_buffer,
				src_image.vk_image, ResourceTracker::GetImageLayout({ src_image }),
				swapchain_image.vk_image, ResourceTracker::GetImageLayout({ swapchain_image }),
				1, &blit_region, VK_FILTER_NEAREST
			);
		}

		void GenerateMips(const VulkanCommandBuffer& command_buffer, const VulkanImage& image)
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

		void TransitionLayout(const VulkanCommandBuffer& command_buffer, const VulkanImageLayoutTransition& layout_info)
		{
			TransitionLayouts(command_buffer, { layout_info });
		}

		void TransitionLayouts(const VulkanCommandBuffer& command_buffer, const std::vector<VulkanImageLayoutTransition> layout_infos)
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

	}

}
