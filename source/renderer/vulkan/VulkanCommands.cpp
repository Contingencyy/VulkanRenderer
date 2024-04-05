#include "Precomp.h"
#include "renderer/vulkan/VulkanCommands.h"
#include "renderer/vulkan/VulkanCommandQueue.h"
#include "renderer/vulkan/VulkanCommandBuffer.h"
#include "renderer/vulkan/VulkanSync.h"
#include "renderer/vulkan/VulkanBackend.h"
#include "renderer/vulkan/VulkanDescriptor.h"
#include "renderer/vulkan/VulkanResourceTracker.h"
#include "renderer/vulkan/VulkanInstance.h"
#include "renderer/vulkan/VulkanUtils.h"

namespace Vulkan
{

	namespace Command
	{

		void BeginRendering(const VulkanCommandBuffer& command_buffer, uint32_t num_color_attachments, const VkRenderingAttachmentInfo* const color_attachments, 
			const VkRenderingAttachmentInfo* const depth_attachment, const VkRenderingAttachmentInfo* const stencil_attachment, uint32_t render_width, uint32_t render_height, int32_t offset_x, int32_t offset_y)
		{
			VkRenderingInfo rendering_info = { VK_STRUCTURE_TYPE_RENDERING_INFO };
			rendering_info.colorAttachmentCount = num_color_attachments;
			rendering_info.pColorAttachments = color_attachments;
			rendering_info.pDepthAttachment = depth_attachment;
			rendering_info.pStencilAttachment = stencil_attachment;
			rendering_info.renderArea.extent = { .width = render_width, .height = render_height };
			rendering_info.renderArea.offset = { .x = offset_x, .y = offset_y };
			rendering_info.viewMask = 0;
			rendering_info.layerCount = 1;
			rendering_info.flags = 0;

			vkCmdBeginRendering(command_buffer.vk_command_buffer, &rendering_info);
		}

		void EndRendering(const VulkanCommandBuffer& command_buffer)
		{
			vkCmdEndRendering(command_buffer.vk_command_buffer);
		}

		void BindPipeline(VulkanCommandBuffer& command_buffer, const VulkanPipeline& pipeline)
		{
			vkCmdBindPipeline(command_buffer.vk_command_buffer, Util::ToVkPipelineBindPoint(pipeline.type), pipeline.vk_pipeline);
			command_buffer.pipeline_bound = pipeline;

			Vulkan::Descriptor::BindDescriptors(command_buffer, command_buffer.pipeline_bound);
		}

		void PushConstants(const VulkanCommandBuffer& command_buffer, VkShaderStageFlags stage_flags, uint32_t byte_offset, uint32_t num_bytes, const void* data)
		{
			vkCmdPushConstants(command_buffer.vk_command_buffer, command_buffer.pipeline_bound.vk_pipeline_layout, stage_flags, byte_offset, num_bytes, data);
		}

		void SetViewport(const VulkanCommandBuffer& command_buffer, uint32_t first_viewport, uint32_t num_viewports, const VkViewport* const vk_viewports)
		{
			vkCmdSetViewport(command_buffer.vk_command_buffer, first_viewport, num_viewports, vk_viewports);
		}

		void SetScissor(const VulkanCommandBuffer& command_buffer, uint32_t first_scissor, uint32_t num_scissors, const VkRect2D* const vk_scissor_rects)
		{
			vkCmdSetScissor(command_buffer.vk_command_buffer, first_scissor, num_scissors, vk_scissor_rects);
		}

		void DrawGeometry(const VulkanCommandBuffer& command_buffer, uint32_t num_vertices, uint32_t num_instances, uint32_t first_vertex, uint32_t first_instance)
		{
			// NOTE: No need to call vkCmdBindVertexBuffers because we use vertex pulling
			vkCmdDraw(command_buffer.vk_command_buffer, num_vertices, num_instances, first_vertex, first_instance);
		}

		void DrawGeometryIndexed(const VulkanCommandBuffer& command_buffer, const VulkanBuffer* const index_buffer,	VkIndexType index_type, uint32_t num_indices,
			uint32_t num_instances, uint32_t first_instance, uint32_t first_index, uint32_t vertex_offset)
		{
			// NOTE: No need to call vkCmdBindVertexBuffers because we use vertex pulling
			if (index_buffer)
				vkCmdBindIndexBuffer(command_buffer.vk_command_buffer, index_buffer->vk_buffer, 0, index_type);

			vkCmdDrawIndexed(command_buffer.vk_command_buffer, num_indices, num_instances, first_index, vertex_offset, first_instance);
		}

		void ClearImage(const VulkanCommandBuffer& command_buffer, const VulkanImage& image, const VkClearColorValue& clear_value)
		{
			VkImageSubresourceRange subresource_range = {};
			subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			subresource_range.baseMipLevel = 0;
			subresource_range.levelCount = image.num_mips;
			subresource_range.baseArrayLayer = 0;
			subresource_range.layerCount = image.num_layers;

			vkCmdClearColorImage(command_buffer.vk_command_buffer, image.vk_image,
				ResourceTracker::GetImageLayout({ .image = image.vk_image }), &clear_value, 1, &subresource_range);
		}

		void ClearImage(const VulkanCommandBuffer& command_buffer, const VulkanImage& image, const VkClearDepthStencilValue& clear_value)
		{
			VkImageSubresourceRange subresource_range = {};
			subresource_range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
			subresource_range.baseMipLevel = 0;
			subresource_range.levelCount = image.num_mips;
			subresource_range.baseArrayLayer = 0;
			subresource_range.layerCount = image.num_layers;

			vkCmdClearDepthStencilImage(command_buffer.vk_command_buffer, image.vk_image,
				ResourceTracker::GetImageLayout({ .image = image.vk_image }), &clear_value, 1, &subresource_range);
		}

		void Dispatch(const VulkanCommandBuffer& command_buffer, uint32_t group_x, uint32_t group_y, uint32_t group_z)
		{
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

			VulkanImageBarrier layout_info = {};
			layout_info.image = image;
			layout_info.new_layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
			ResourceTracker::UpdateImageAccessAndStageFlags(layout_info);
		}

		void BufferMemoryBarrier(const VulkanCommandBuffer& command_buffer, const VulkanBufferBarrier& buffer_barrier)
		{
			BufferMemoryBarriers(command_buffer, { buffer_barrier });
		}

		void BufferMemoryBarriers(const VulkanCommandBuffer& command_buffer, const std::vector<VulkanBufferBarrier>& buffer_barriers)
		{
			std::vector<VkBufferMemoryBarrier2> vk_buffer_memory_barriers(buffer_barriers.size());

			for (uint32_t i = 0; i < buffer_barriers.size(); ++i)
			{
				vk_buffer_memory_barriers[i] = ResourceTracker::BufferMemoryBarrier(buffer_barriers[i]);
				ResourceTracker::UpdateBufferAccessAndStageFlags(buffer_barriers[i]);
			}

			VkDependencyInfo dependency_info = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
			dependency_info.bufferMemoryBarrierCount = static_cast<uint32_t>(vk_buffer_memory_barriers.size());
			dependency_info.pBufferMemoryBarriers = vk_buffer_memory_barriers.data();
			dependency_info.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			vkCmdPipelineBarrier2(command_buffer.vk_command_buffer, &dependency_info);
		}

		void TransitionLayout(const VulkanCommandBuffer& command_buffer, const VulkanImageBarrier& image_barrier)
		{
			TransitionLayouts(command_buffer, { image_barrier });
		}

		void TransitionLayouts(const VulkanCommandBuffer& command_buffer, const std::vector<VulkanImageBarrier>& image_barriers)
		{
			std::vector<VkImageMemoryBarrier2> vk_image_memory_barriers(image_barriers.size());

			for (uint32_t i = 0; i < image_barriers.size(); ++i)
			{
				vk_image_memory_barriers[i] = ResourceTracker::ImageMemoryBarrier(image_barriers[i]);
				ResourceTracker::UpdateImageAccessAndStageFlags(image_barriers[i]);
			}

			VkDependencyInfo dependency_info = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
			dependency_info.imageMemoryBarrierCount = static_cast<uint32_t>(vk_image_memory_barriers.size());
			dependency_info.pImageMemoryBarriers = vk_image_memory_barriers.data();
			dependency_info.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			vkCmdPipelineBarrier2(command_buffer.vk_command_buffer, &dependency_info);
		}

	}

}
