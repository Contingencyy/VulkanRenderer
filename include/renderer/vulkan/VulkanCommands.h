#pragma once
#include "renderer/vulkan/VulkanTypes.h"

namespace Vulkan
{

	namespace Command
	{

		void BeginRendering(const VulkanCommandBuffer& command_buffer, uint32_t num_color_attachments, const VkRenderingAttachmentInfo* const color_attachments,
			const VkRenderingAttachmentInfo* const depth_attachment, const VkRenderingAttachmentInfo* const stencil_attachment, uint32_t render_width, uint32_t render_height, int32_t offset_x = 0, int32_t offset_y = 0);
		void EndRendering(const VulkanCommandBuffer& command_buffer);
		void BindPipeline(VulkanCommandBuffer& command_buffer, const VulkanPipeline& pipeline);
		void PushConstants(const VulkanCommandBuffer& command_buffer, VkShaderStageFlags stage_flags, uint32_t byte_offset, uint32_t num_bytes, const void* data);

		void SetViewport(const VulkanCommandBuffer& command_buffer, uint32_t first_viewport, uint32_t num_viewports, const VkViewport* const vk_viewports);
		void SetScissor(const VulkanCommandBuffer& command_buffer, uint32_t first_scissor, uint32_t num_scissors, const VkRect2D* const vk_scissor_rects);

		void DrawGeometry(const VulkanCommandBuffer& command_buffer, uint32_t num_vertex_buffers, const VulkanBuffer* vertex_buffers,
			uint32_t num_vertices, uint32_t num_instances = 1, uint32_t first_vertex = 0, uint32_t first_instance = 0);
		void DrawGeometryIndexed(const VulkanCommandBuffer& command_buffer, uint32_t num_vertex_buffers, const VulkanBuffer* const vertex_buffers, const VulkanBuffer* const index_buffer,
			uint32_t index_byte_size = 4, uint32_t num_instances = 1, uint32_t first_instance = 0, uint32_t first_index = 0, uint32_t vertex_offset = 0);

		void ClearImage(const VulkanCommandBuffer& command_buffer, const VulkanImage& image, const VkClearColorValue& clear_value);
		void ClearImage(const VulkanCommandBuffer& command_buffer, const VulkanImage& image, const VkClearDepthStencilValue& clear_value);
		void Dispatch(const VulkanCommandBuffer& command_buffer, uint32_t group_x, uint32_t group_y, uint32_t group_z);

		void CopyBuffers(const VulkanCommandBuffer& command_buffer, const VulkanBuffer& src_buffer, uint64_t src_offset, const VulkanBuffer& dst_buffer, uint64_t dst_offset, uint64_t num_bytes);
		void CopyFromBuffer(const VulkanCommandBuffer& command_buffer, const VulkanBuffer& src_buffer, uint64_t src_offset, const VulkanImage& dst_image, uint32_t dst_width, uint32_t dst_height);
		void CopyImages(const VulkanCommandBuffer& command_buffer, const VulkanImage& src_image, const VulkanImage& dst_image);
		void GenerateMips(const VulkanCommandBuffer& command_buffer, const VulkanImage& image);

		void BufferMemoryBarrier(const VulkanCommandBuffer& command_buffer, const VulkanBufferBarrier& buffer_barrier);
		void BufferMemoryBarriers(const VulkanCommandBuffer& command_buffer, const std::vector<VulkanBufferBarrier>& buffer_barriers);

		void TransitionLayout(const VulkanCommandBuffer& command_buffer, const VulkanImageBarrier& image_barrier);
		void TransitionLayouts(const VulkanCommandBuffer& command_buffers, const std::vector<VulkanImageBarrier>& image_barriers);

	}

}
