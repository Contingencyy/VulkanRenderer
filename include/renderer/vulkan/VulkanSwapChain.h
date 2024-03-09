#pragma once
#include "renderer/vulkan/VulkanTypes.h"

namespace Vulkan
{

	void CreateSwapChain();
	void DestroySwapChain();

	bool SwapChainAcquireNextImage();
	bool SwapChainPresent(const std::vector<VulkanFence>& wait_fences);
	void SwapChainRecreate();
	void SwapChainCopyFrom(VulkanCommandBuffer& command_buffer, VulkanImage src_image);
	void SwapChainSetVSync(bool enabled);
	bool SwapChainIsVSyncEnabled();

}
