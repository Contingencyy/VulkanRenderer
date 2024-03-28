#include "Precomp.h"
#include "renderer/vulkan/VulkanCommandPool.h"
#include "renderer/vulkan/VulkanInstance.h"

namespace Vulkan
{

	namespace CommandPool
	{

		VulkanCommandBuffer AllocateCommandBuffer(const VulkanCommandPool& command_pool)
		{
			VkCommandBufferAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
			alloc_info.commandPool = command_pool.vk_command_pool;
			alloc_info.commandBufferCount = 1;
			alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

			VkCommandBuffer vk_command_buffer;
			vkAllocateCommandBuffers(vk_inst.device, &alloc_info, &vk_command_buffer);

			VulkanCommandBuffer command_buffer = {};
			command_buffer.vk_command_buffer = vk_command_buffer;
			command_buffer.type = command_pool.type;

			return command_buffer;
		}

		std::vector<VulkanCommandBuffer> AllocateCommandBuffers(const VulkanCommandPool& command_pool, uint32_t num_buffers)
		{
			VkCommandBufferAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
			alloc_info.commandPool = command_pool.vk_command_pool;
			alloc_info.commandBufferCount = num_buffers;
			alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

			std::vector<VkCommandBuffer> vk_command_buffers(num_buffers);
			vkAllocateCommandBuffers(vk_inst.device, &alloc_info, vk_command_buffers.data());

			std::vector<VulkanCommandBuffer> command_buffers(num_buffers);
			for (uint32_t i = 0; i < vk_command_buffers.size(); ++i)
			{
				command_buffers[i].vk_command_buffer = vk_command_buffers[i];
				command_buffers[i].type = command_pool.type;
			}

			return command_buffers;
		}

		void FreeCommandBuffer(const VulkanCommandPool& command_pool, VulkanCommandBuffer& command_buffer)
		{
			vkFreeCommandBuffers(vk_inst.device, command_pool.vk_command_pool, 1, &command_buffer.vk_command_buffer);
			command_buffer = {};
		}

		void FreeCommandBuffers(const VulkanCommandPool& command_pool, uint32_t num_buffers, VulkanCommandBuffer* const command_buffers)
		{
			std::vector<VkCommandBuffer> vk_command_buffers;
			for (uint32_t i = 0; i < num_buffers; ++i)
			{
				vk_command_buffers.push_back(command_buffers[i].vk_command_buffer);
				command_buffers[i] = {};
			}

			vkFreeCommandBuffers(vk_inst.device, command_pool.vk_command_pool,
				static_cast<uint32_t>(vk_command_buffers.size()), vk_command_buffers.data());
		}

		VulkanCommandPool Create(const VulkanCommandQueue& command_queue)
		{
			VkCommandPoolCreateInfo command_pool_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
			command_pool_info.queueFamilyIndex = command_queue.queue_family_index;
			command_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

			VkCommandPool vk_command_pool;
			vkCreateCommandPool(vk_inst.device, &command_pool_info, nullptr, &vk_command_pool);

			VulkanCommandPool command_pool = {};
			command_pool.vk_command_pool = vk_command_pool;
			command_pool.type = command_queue.type;

			return command_pool;
		}

		void Destroy(const VulkanCommandPool& command_pool)
		{
			vkDestroyCommandPool(vk_inst.device, command_pool.vk_command_pool, nullptr);
		}

		void Reset(const VulkanCommandPool& command_pool)
		{
			vkResetCommandPool(vk_inst.device, command_pool.vk_command_pool, 0);
		}

	}

}
