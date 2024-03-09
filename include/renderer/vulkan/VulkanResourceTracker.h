#pragma once
#include "renderer/vulkan/VulkanBackend.h"

namespace Vulkan
{
	
	namespace ResourceTracker
	{

		void Init();
		void Exit();

		void TrackBuffer(VulkanBuffer buffer);
		void TrackBufferTemp(VulkanBuffer buffer, VulkanFence fence);
		void RemoveBuffer(VulkanBuffer buffer);

		void TrackImage(VulkanImage image, VkImageLayout layout);
		void TrackImageTemp(VulkanImage image, VulkanFence fence);
		void RemoveImage(VulkanImage image);

		VkImageMemoryBarrier2 ImageMemoryBarrier(const ImageLayoutTransition& layout_info);
		void UpdateImageLayout(const ImageLayoutTransition& layout_info);
		VkImageLayout GetImageLayout(const ImageLayoutTransition& layout_info);

		void ReleaseStaleTempResources();

	};

}
