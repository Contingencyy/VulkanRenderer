#pragma once
#include "renderer/vulkan/VulkanTypes.h"

#include <vector>

namespace Vulkan
{

	namespace CommandPool
	{

		VulkanCommandPool Create(const VulkanCommandQueue& command_queue);
		void Destroy(const VulkanCommandPool& command_pool);
		void Reset(const VulkanCommandPool& command_pool);

		VulkanCommandBuffer AllocateCommandBuffer(const VulkanCommandPool& command_pool);
		std::vector<VulkanCommandBuffer> AllocateCommandBuffers(const VulkanCommandPool& command_pool, uint32_t num_buffers);
		void FreeCommandBuffer(const VulkanCommandPool& command_pool, VulkanCommandBuffer& command_buffer);
		void FreeCommandBuffers(const VulkanCommandPool& command_pool, uint32_t num_buffers, VulkanCommandBuffer* const command_buffers);

	}

}
