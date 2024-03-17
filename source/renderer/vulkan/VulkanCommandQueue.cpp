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
			// Does nothing..
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
			std::vector<VkCommandBufferSubmitInfo> command_buffer_submit_infos(num_buffers);
			std::vector<VkSemaphoreSubmitInfo> wait_submit_infos;
			std::vector<VkSemaphoreSubmitInfo> signal_submit_infos;

			// Add all vk command bufers into a vector
			for (uint32_t i = 0; i < num_buffers; ++i)
			{
				command_buffer_submit_infos[i].sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
				command_buffer_submit_infos[i].commandBuffer = command_buffers[i].vk_command_buffer;
				command_buffer_submit_infos[i].deviceMask = 0;

				// Add all wait fences into a vector
				for (uint32_t j = 0; j < command_buffers[i].wait_fences.size(); ++j)
				{
					VkSemaphoreSubmitInfo wait_submit_info = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
					wait_submit_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
					wait_submit_info.semaphore = command_buffers[i].wait_fences[j].vk_semaphore;
					wait_submit_info.value = command_buffers[i].wait_fences[j].fence_value;
					wait_submit_info.stageMask = command_buffers[i].wait_fences[j].stage_flags;
					wait_submit_info.deviceIndex = 0;

					wait_submit_infos.push_back(wait_submit_info);
				}
			}

			// Add the command queue fence to the signal submissions
			uint64_t queue_fence_value = ++queue.fence.fence_value;
			{
				VkSemaphoreSubmitInfo signal_submit_info = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
				signal_submit_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
				signal_submit_info.semaphore = queue.fence.vk_semaphore;
				signal_submit_info.value = queue_fence_value;
				signal_submit_info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
				signal_submit_info.deviceIndex = 0;

				signal_submit_infos.push_back(signal_submit_info);
			}

			// Add additional signal fences to the signal submissions
			// E.g. used for the binary semaphores to indicate that rendering has finished
			for (uint32_t i = 0; i < num_signal_fences; ++i)
			{
				uint64_t fence_value = signal_fences[i].type == VULKAN_FENCE_TYPE_TIMELINE ? ++signal_fences[i].fence_value : 0;

				VkSemaphoreSubmitInfo signal_submit_info = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
				signal_submit_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
				signal_submit_info.semaphore = signal_fences[i].vk_semaphore;
				signal_submit_info.value = fence_value;
				signal_submit_info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
				signal_submit_info.deviceIndex = 0;

				signal_submit_infos.push_back(signal_submit_info);
			}

			VkSubmitInfo2 submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
			submit_info.commandBufferInfoCount = static_cast<uint32_t>(command_buffer_submit_infos.size());
			submit_info.pCommandBufferInfos = command_buffer_submit_infos.data();
			submit_info.waitSemaphoreInfoCount = static_cast<uint32_t>(wait_submit_infos.size());
			submit_info.pWaitSemaphoreInfos = wait_submit_infos.data();
			submit_info.signalSemaphoreInfoCount = static_cast<uint32_t>(signal_submit_infos.size());
			submit_info.pSignalSemaphoreInfos = signal_submit_infos.data();
			submit_info.flags = 0;

			vkQueueSubmit2(queue.vk_queue, 1, &submit_info, nullptr);
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
