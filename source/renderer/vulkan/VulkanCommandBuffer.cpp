#include "Precomp.h"
#include "renderer/vulkan/VulkanCommandBuffer.h"
#include "renderer/vulkan/VulkanInstance.h"

namespace Vulkan
{

	namespace CommandBuffer
	{

		void BeginRecording(const VulkanCommandBuffer& command_buffer)
		{
			VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
			begin_info.pInheritanceInfo = nullptr;
			begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

			vkBeginCommandBuffer(command_buffer.vk_command_buffer, &begin_info);
		}

		void EndRecording(const VulkanCommandBuffer& command_buffer)
		{
			vkEndCommandBuffer(command_buffer.vk_command_buffer);
		}

		void Reset(VulkanCommandBuffer& command_buffer)
		{
			command_buffer.wait_fences.clear();
			vkResetCommandBuffer(command_buffer.vk_command_buffer, 0);
		}

		void AddWait(VulkanCommandBuffer& command_buffer, const VulkanFence& fence, VkPipelineStageFlags2 stage_flags, uint64_t fence_value)
		{
			command_buffer.wait_fences.emplace_back(fence.type, fence.vk_semaphore, stage_flags, fence_value);
		}

	}

}
