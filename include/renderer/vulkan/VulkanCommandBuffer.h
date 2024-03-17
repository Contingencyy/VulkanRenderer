#pragma once
#include "renderer/vulkan/VulkanTypes.h"

namespace Vulkan
{

	namespace CommandBuffer
	{

		void BeginRecording(const VulkanCommandBuffer& command_buffer);
		void EndRecording(const VulkanCommandBuffer& command_buffer);
		void Reset(VulkanCommandBuffer& command_buffer);

		void AddWait(VulkanCommandBuffer& command_buffer, const VulkanFence& fence, VkPipelineStageFlags2 stage_flags, uint64_t fence_value = 0);

	}

}
