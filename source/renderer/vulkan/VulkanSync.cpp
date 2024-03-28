#include "Precomp.h"
#include "renderer/vulkan/VulkanSync.h"
#include "renderer/vulkan/VulkanInstance.h"
#include "renderer/vulkan/VulkanUtils.h"

namespace Vulkan
{

	namespace Sync
	{

		VulkanFence CreateFence(VulkanFenceType type, uint64_t initial_fence_value)
		{
			VkSemaphoreTypeCreateInfo vk_semaphore_create_info = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
			vk_semaphore_create_info.semaphoreType = type == VULKAN_FENCE_TYPE_TIMELINE ? VK_SEMAPHORE_TYPE_TIMELINE : VK_SEMAPHORE_TYPE_BINARY;
			vk_semaphore_create_info.initialValue = initial_fence_value;

			VkSemaphoreCreateInfo semaphore_info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
			semaphore_info.flags = 0;
			semaphore_info.pNext = &vk_semaphore_create_info;

			VkSemaphore vk_semaphore = VK_NULL_HANDLE;
			VkCheckResult(vkCreateSemaphore(vk_inst.device, &semaphore_info, nullptr, &vk_semaphore));

			VulkanFence fence = {};
			fence.type = type;
			fence.vk_semaphore = vk_semaphore;
			fence.fence_value = initial_fence_value;

			return fence;
		}

		void DestroyFence(VulkanFence& fence)
		{
			vkDestroySemaphore(vk_inst.device, fence.vk_semaphore, nullptr);
			fence = {};
		}

		void SignalFence(VulkanFence& fence)
		{
			VkSemaphoreSignalInfo signal_info = { VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO };
			signal_info.semaphore = fence.vk_semaphore;
			signal_info.value = ++fence.fence_value;
			vkSignalSemaphore(vk_inst.device, &signal_info);
		}

		void WaitOnFence(const VulkanFence& fence, uint64_t wait_value)
		{
			VkSemaphoreWaitInfo wait_info = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
			wait_info.semaphoreCount = 1;
			wait_info.pSemaphores = &fence.vk_semaphore;
			wait_info.pValues = &wait_value;
			wait_info.flags = 0;
			vkWaitSemaphores(vk_inst.device, &wait_info, UINT64_MAX);
		}

		uint64_t GetFenceValue(const VulkanFence& fence)
		{
			uint64_t fence_value;
			vkGetSemaphoreCounterValue(vk_inst.device, fence.vk_semaphore, &fence_value);
			return fence_value;
		}

		bool IsFenceCompleted(const VulkanFence& fence)
		{
			return (fence.fence_value >= GetFenceValue(fence));
		}

	}

}
