#pragma once
#include "renderer/vulkan/VulkanTypes.h"

namespace Vulkan
{

	namespace SwapChain
	{

		void Create(uint32_t output_width, uint32_t output_height);
		void Destroy();

		VkResult AcquireNextImage();
		VkResult Present(const std::vector<VulkanFence>& wait_fences);

		void SetVSync(bool enabled);
		bool IsVSyncEnabled();

	}

}
