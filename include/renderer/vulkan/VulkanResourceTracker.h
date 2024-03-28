#pragma once
#include "renderer/vulkan/VulkanTypes.h"

namespace Vulkan
{
	
	namespace ResourceTracker
	{

		void Init();
		void Exit();

		void TrackBuffer(const VulkanBuffer& buffer);
		void TrackBufferTemp(const VulkanBuffer& buffer, const VulkanFence& fence, uint64_t fence_value);
		void RemoveBuffer(const VulkanBuffer& buffer);

		VkBufferMemoryBarrier2 BufferMemoryBarrier(const VulkanBufferBarrier& barrier);
		void UpdateBufferAccessAndStageFlags(const VulkanBufferBarrier& barrier);

		void TrackImage(const VulkanImage& image, VkImageLayout layout);
		void TrackImageTemp(const VulkanImage& image, const VulkanFence& fence, uint64_t fence_value);
		void RemoveImage(const VulkanImage& image);

		VkImageMemoryBarrier2 ImageMemoryBarrier(const VulkanImageBarrier& barrier);
		void UpdateImageAccessAndStageFlags(const VulkanImageBarrier& barrier);
		VkImageLayout GetImageLayout(const VulkanImageBarrier& barrier);

		void ReleaseStaleTempResources();

	};

}
