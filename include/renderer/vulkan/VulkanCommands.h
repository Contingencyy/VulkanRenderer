#pragma once
#include "renderer/vulkan/VulkanTypes.h"

namespace Vulkan
{

	namespace Command
	{

		inline void SetViewport(const VulkanCommandBuffer& command_buffer, uint32_t first_viewport, uint32_t num_viewports, const VkViewport* const vk_viewports);
		inline void SetScissor(const VulkanCommandBuffer& command_buffer, uint32_t first_scissor, uint32_t num_scissors, const VkRect2D* const vk_scissor_rects);
		inline void DrawGeometry(const VulkanCommandBuffer& command_buffer, uint32_t num_vertex_buffers, const VulkanBuffer* vertex_buffers,
			uint32_t num_vertices, uint32_t num_instances = 1, uint32_t first_vertex = 0, uint32_t first_instance = 0);
		inline void DrawGeometryIndexed(const VulkanCommandBuffer& command_buffer, uint32_t num_vertex_buffers, const VulkanBuffer* const vertex_buffers, const VulkanBuffer* const index_buffer,
			uint32_t index_byte_size = 4, uint32_t num_instances = 1, uint32_t first_instance = 0, uint32_t first_index = 0, uint32_t vertex_offset = 0);

		inline void Dispatch(const VulkanCommandBuffer& command_buffer, uint32_t group_x, uint32_t group_y, uint32_t group_z);

		inline void CopyBuffers(const VulkanCommandBuffer& command_buffer, const VulkanBuffer& src_buffer, uint64_t src_offset, const VulkanBuffer& dst_buffer, uint64_t dst_offset, uint64_t num_bytes);
		void CopyFromBuffer(const VulkanCommandBuffer& command_buffer, const VulkanBuffer& src_buffer, uint64_t src_offset, const VulkanImage& dst_image, uint32_t dst_width, uint32_t dst_height);
		void CopyImages(const VulkanCommandBuffer& command_buffer, const VulkanImage& src_image, const VulkanImage& dst_image);
		void GenerateMips(const VulkanCommandBuffer& command_buffer, const VulkanImage& image);

		void TransitionLayout(const VulkanCommandBuffer& command_buffer, const VulkanImageLayoutTransition& layout_info);
		void TransitionLayouts(const VulkanCommandBuffer& command_buffers, const std::vector<VulkanImageLayoutTransition> layout_infos);

	}

}
