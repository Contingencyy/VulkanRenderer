#pragma once
#include "renderer/vulkan/VulkanTypes.h"

#include <vector>

namespace Vulkan
{

	namespace CommandQueue
	{

		VulkanCommandQueue Create(VulkanCommandBufferType type, uint32_t queue_family_index, uint32_t queue_index);
		void Destroy(VulkanCommandQueue& command_queue);

		uint64_t Execute(VulkanCommandQueue& queue, const VulkanCommandBuffer& command_buffer,
			uint32_t num_signal_fences = 0, VulkanFence* const signal_fences = nullptr);
		uint64_t ExecuteBlocking(VulkanCommandQueue& queue, VulkanCommandBuffer& command_buffer,
			uint32_t num_signal_fences = 0, VulkanFence* const signal_fences = nullptr);
		uint64_t Execute(VulkanCommandQueue& queue, uint32_t num_buffers, const VulkanCommandBuffer* const command_buffers,
			uint32_t num_signal_fences = 0, VulkanFence* const signal_fences = nullptr);

		void WaitFenceValue(const VulkanCommandQueue& command_queue, uint64_t fence_value);
		void WaitIdle(const VulkanCommandQueue& command_queue);
		bool IsIdle(const VulkanCommandQueue& command_queue);

	}

}
