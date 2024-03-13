#pragma once
#include "renderer/vulkan/VulkanTypes.h"

namespace Vulkan
{

	namespace CommandBuffer
	{

		void BeginRecording(const VulkanCommandBuffer& command_buffer);
		void EndRecording(const VulkanCommandBuffer& command_buffer);
		void Reset(const VulkanCommandBuffer& command_buffer);

		void AddWait(const VulkanCommandBuffer& command_buffer, const VulkanFence& fence, VkPipelineStageFlags2 stage_flags, uint64_t fence_value = 0);
		void AddSignal(const VulkanCommandBuffer& command_buffer, const VulkanFence& fence, uint64_t fence_value = 0);

	}

}
