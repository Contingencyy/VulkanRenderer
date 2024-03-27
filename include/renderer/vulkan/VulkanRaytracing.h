#pragma once
#include "renderer/vulkan/VulkanTypes.h"

namespace Vulkan
{

	namespace Raytracing
	{

		VulkanBuffer BuildBLAS(const VulkanCommandBuffer& command_buffer, const VulkanBuffer& vertex_buffer, const VulkanBuffer& index_buffer, VulkanBuffer& scratch_buffer,
			uint32_t first_vertex, uint32_t num_vertices, uint32_t vertex_stride, VkIndexType index_type, const std::string& name);
		VulkanBuffer BuildTLAS(const VulkanCommandBuffer& command_buffer);

	}

}
