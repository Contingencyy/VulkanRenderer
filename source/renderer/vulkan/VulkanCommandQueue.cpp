#include "Precomp.h"
#include "renderer/vulkan/VulkanCommandQueue.h"
#include "renderer/vulkan/VulkanCommandBuffer.h"
#include "renderer/vulkan/VulkanSync.h"
#include "renderer/vulkan/VulkanInstance.h"
#include "renderer/vulkan/VulkanUtils.h"

namespace Vulkan
{

	namespace CommandQueue
	{

		VulkanCommandQueue Create(VulkanCommandBufferType type, uint32_t queue_family_index, uint32_t queue_index)
		{
			VkQueue vk_queue;
			vkGetDeviceQueue(vk_inst.device, queue_index, 0, &vk_queue);

			VulkanCommandQueue command_queue = {};
			command_queue.type = type;
			command_queue.vk_queue = vk_queue;
			command_queue.queue_family_index = queue_index;
			command_queue.fence = Sync::CreateFence(VULKAN_FENCE_TYPE_TIMELINE, 0);

			return command_queue;
		}

		void Destroy(const VulkanCommandQueue& command_queue)
		{
			Sync::DestroyFence(command_queue.fence);
		}

		uint64_t Execute(VulkanCommandQueue& queue, const VulkanCommandBuffer& command_buffer,
			uint32_t num_signal_fences, VulkanFence* const signal_fences)
		{
			return Execute(queue, 1, &command_buffer, num_signal_fences, signal_fences);
		}

		uint64_t ExecuteBlocking(VulkanCommandQueue& queue, VulkanCommandBuffer& command_buffer,
			uint32_t num_signal_fences, VulkanFence* const signal_fences)
		{
			uint64_t fence_value = Execute(queue, 1, &command_buffer, num_signal_fences, signal_fences);
			Sync::WaitOnFence(queue.fence, fence_value);

			return fence_value;
		}

		uint64_t Execute(VulkanCommandQueue& queue, uint32_t num_buffers, const VulkanCommandBuffer* const command_buffers,
			uint32_t num_signal_fences, VulkanFence* const signal_fences)
		{
			std::vector<VkCommandBuffer> vk_command_buffers(num_buffers);

			std::vector<VkSemaphore> vk_wait_semaphores;
			std::vector<VkPipelineStageFlags> vk_wait_stages;
			std::vector<uint64_t> wait_fence_values;

			std::vector<VkSemaphore> vk_signal_semaphores;
			std::vector<uint64_t> signal_fence_values;

			// Go through the command buffers and their wait fences
			for (uint32_t i = 0; i < num_buffers; ++i)
			{
				vk_command_buffers[i] = command_buffers[i].vk_command_buffer;

				for (uint32_t wait_fence = 0; wait_fence < command_buffers[i].wait_fences.size(); ++wait_fence)
				{
					vk_wait_semaphores.push_back(command_buffers[i].wait_fences[wait_fence].vk_semaphore);
					vk_wait_stages.push_back(command_buffers[i].wait_fences[wait_fence].stage_flags);
					wait_fence_values.push_back(command_buffers[i].wait_fences[wait_fence].fence_value);
				}
			}

			// Add the queue's timeline fence to signal submission
			vk_signal_semaphores.push_back(queue.fence.vk_semaphore);
			uint64_t queue_fence_value = ++queue.fence.fence_value;
			signal_fence_values.push_back(queue_fence_value);

			// Add additional fences to signal submission
			for (uint32_t i = 0; i < num_signal_fences; ++i)
			{
				vk_signal_semaphores.push_back(signal_fences[i].vk_semaphore);
				signal_fence_values.push_back(signal_fences[i].fence_value);
			}

			VkTimelineSemaphoreSubmitInfo timeline_semaphores_submit_info = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
			timeline_semaphores_submit_info.waitSemaphoreValueCount = static_cast<uint32_t>(wait_fence_values.size());
			timeline_semaphores_submit_info.pWaitSemaphoreValues = wait_fence_values.data();
			timeline_semaphores_submit_info.signalSemaphoreValueCount = static_cast<uint32_t>(signal_fence_values.size());
			timeline_semaphores_submit_info.pSignalSemaphoreValues = signal_fence_values.data();

			VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
			submit_info.commandBufferCount = static_cast<uint32_t>(vk_command_buffers.size());
			submit_info.pCommandBuffers = vk_command_buffers.data();
			submit_info.waitSemaphoreCount = static_cast<uint32_t>(vk_wait_semaphores.size());
			submit_info.pWaitSemaphores = vk_wait_semaphores.data();
			submit_info.pWaitDstStageMask = vk_wait_stages.data();
			submit_info.signalSemaphoreCount = static_cast<uint32_t>(vk_signal_semaphores.size());
			submit_info.pSignalSemaphores = vk_signal_semaphores.data();
			submit_info.pNext = &timeline_semaphores_submit_info;

			VkCheckResult(vkQueueSubmit(queue.vk_queue, 1, &submit_info, VK_NULL_HANDLE));

			return queue_fence_value;
		}

		void WaitFenceValue(const VulkanCommandQueue& command_queue, uint64_t fence_value)
		{
			Sync::WaitOnFence(command_queue.fence, fence_value);
		}

		void WaitIdle(const VulkanCommandQueue& command_queue)
		{
			VkCheckResult(vkQueueWaitIdle(command_queue.vk_queue));
		}

		bool IsIdle(const VulkanCommandQueue& command_queue)
		{
			return (
				command_queue.fence.fence_value >= Sync::GetFenceValue(command_queue.fence)
			);
		}

	}

}
