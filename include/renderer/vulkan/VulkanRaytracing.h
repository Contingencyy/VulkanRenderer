#pragma once
#include "renderer/vulkan/VulkanTypes.h"

namespace Vulkan
{

	namespace Raytracing
	{

		VulkanBuffer BuildBLAS(const VulkanCommandBuffer& command_buffer, const VulkanBuffer& vertex_buffer, const VulkanBuffer& index_buffer, VulkanBuffer& scratch_buffer,
			uint32_t num_vertices, uint32_t vertex_stride, uint32_t num_triangles, VkIndexType index_type, const std::string& name);
		VulkanBuffer BuildTLAS(const VulkanCommandBuffer& command_buffer, VulkanBuffer& scratch_buffer, VulkanBuffer& instance_buffer,
			uint32_t num_blas, const VulkanBuffer* const blas_buffers, const VkTransformMatrixKHR* const blas_transforms, const std::string& name);

	}

}
