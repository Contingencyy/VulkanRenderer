#pragma once
#include "renderer/vulkan/VulkanTypes.h"

namespace Vulkan
{

	VulkanFence CreateFence(VulkanFenceType type, uint64_t initial_fence_value = 0);
	void DestroyFence(VulkanFence& fence);

	void SignalFence(VulkanFence& fence);
	void WaitOnFence(const VulkanFence& fence, uint64_t wait_value);

	uint64_t GetFenceValue(const VulkanFence& fence);
	bool GetFenceCompleted(const VulkanFence& fence);

}
