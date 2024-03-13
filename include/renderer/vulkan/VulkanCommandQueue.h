#pragma once
#include "renderer/vulkan/VulkanTypes.h"

namespace Vulkan
{

	namespace CommandQueue
	{

		VulkanCommandQueue Create(VulkanCommandBufferType type, uint32_t queue_family_index, uint32_t queue_index);
		void Destroy(const VulkanCommandQueue& command_queue);

		void Execute(const VulkanCommandQueue& queue, const VulkanCommandBuffer& command_buffer);
		void Execute(const VulkanCommandQueue& queue, uint32_t num_buffers, const VulkanCommandBuffer* const command_buffers);
		// NOTE: The VulkanObject structs are only meant to be neat useful little wrappers but should never assume the usage of
		// the underlying vulkan object. This function is right on the edge of this. Should the VulkanCommandQueue be concerned
		// with being able to block after execution, or should the high-level renderer introduce this instead? These functions
		// here are only basic building blocks to make the usage of Vulkan slightly more convenient.
		void ExecuteBlocking(const VulkanCommandQueue& queue, const VulkanCommandBuffer& command_buffer, VulkanFence& fence);

		void WaitIdle(const VulkanCommandQueue& command_queue);

	}

}
