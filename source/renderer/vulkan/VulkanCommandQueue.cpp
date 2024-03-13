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
			command_queue.vk_queue = vk_queue;
			command_queue.queue_family_index = queue_index;

			return command_queue;
		}

		void Destroy(const VulkanCommandQueue& command_queue)
		{
			// Does nothing..
		}

		void Execute(const VulkanCommandQueue& queue, const VulkanCommandBuffer& command_buffer)
		{
			Execute(queue, 1, &command_buffer);
		}

		void Execute(const VulkanCommandQueue& queue, uint32_t num_buffers, const VulkanCommandBuffer* const command_buffers)
		{
			std::vector<VkCommandBufferSubmitInfo> command_buffer_submit_infos;
			std::vector<VkSemaphoreSubmitInfo> wait_submit_infos;
			std::vector<VkSemaphoreSubmitInfo> signal_submit_infos;

			for (uint32_t i = 0; i < num_buffers; ++i)
			{
				command_buffer_submit_infos[i].sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
				command_buffer_submit_infos[i].commandBuffer = command_buffers[i].vk_command_buffer;
				command_buffer_submit_infos[i].deviceMask = 0;

				for (uint32_t j = 0; j < command_buffers[i].wait_fences.size(); ++j)
				{
					wait_submit_infos[i].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
					wait_submit_infos[i].semaphore = command_buffers[i].wait_fences[j].vk_semaphore;
					wait_submit_infos[i].value = command_buffers[i].wait_fences[j].fence_value;
					wait_submit_infos[i].stageMask = command_buffers[i].wait_fences[j].stage_flags;
					wait_submit_infos[i].deviceIndex = 0;
				}

				for (uint32_t j = 0; j < command_buffers[i].signal_fences.size(); ++j)
				{
					signal_submit_infos[i].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
					signal_submit_infos[i].semaphore = command_buffers[i].signal_fences[j].vk_semaphore;
					signal_submit_infos[i].value = command_buffers[i].signal_fences[j].fence_value;
					signal_submit_infos[i].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
					signal_submit_infos[i].deviceIndex = 0;
				}
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
		}

		void ExecuteBlocking(const VulkanCommandQueue& queue, const VulkanCommandBuffer& command_buffer, VulkanFence& fence)
		{
			CommandBuffer::AddSignal(command_buffer, fence, ++fence.fence_value);
			Execute(queue, command_buffer);

			Sync::WaitOnFence(fence, fence.fence_value);
		}

		void WaitIdle(const VulkanCommandQueue& command_queue)
		{
			VkCheckResult(vkQueueWaitIdle(command_queue.vk_queue));
		}

	}

}
