#include "Precomp.h"
#include "renderer/vulkan/VulkanDeviceMemory.h"
#include "renderer/vulkan/VulkanUtils.h"
#include "renderer/vulkan/VulkanInstance.h"

namespace Vulkan
{

	namespace DeviceMemory
	{

		VulkanMemory Allocate(const VulkanBuffer& buffer, const BufferCreateInfo& buffer_info)
		{
			VkMemoryRequirements mem_req = {};
			vkGetBufferMemoryRequirements(vk_inst.device, buffer.vk_buffer, &mem_req);

			VkMemoryAllocateFlagsInfo alloc_flags = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO };
			alloc_flags.deviceMask = 0;
			alloc_flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

			VkMemoryAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
			alloc_info.allocationSize = mem_req.size;
			alloc_info.memoryTypeIndex = Util::FindMemoryType(mem_req.memoryTypeBits, Util::ToVkMemoryPropertyFlags(buffer_info.memory_flags));
			alloc_info.pNext = &alloc_flags;

			VkDeviceMemory vk_device_memory = VK_NULL_HANDLE;
			VkCheckResult(vkAllocateMemory(vk_inst.device, &alloc_info, nullptr, &vk_device_memory));
			VkCheckResult(vkBindBufferMemory(vk_inst.device, buffer.vk_buffer, vk_device_memory, 0));

			Vulkan::DebugNameObject((uint64_t)vk_device_memory, VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT, buffer_info.name);

			VulkanMemory memory = {};
			memory.vk_device_memory = vk_device_memory;
			memory.vk_memory_flags = Util::ToVkMemoryPropertyFlags(buffer_info.memory_flags);
			memory.vk_memory_index = alloc_info.memoryTypeIndex;

			return memory;
		}

		VulkanMemory Allocate(const VulkanImage& image, const TextureCreateInfo& texture_info)
		{
			VkMemoryRequirements mem_req = {};
			vkGetImageMemoryRequirements(vk_inst.device, image.vk_image, &mem_req);

			VkMemoryAllocateFlagsInfo alloc_flags = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO };
			alloc_flags.deviceMask = 0;
			alloc_flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

			VkMemoryAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
			alloc_info.allocationSize = mem_req.size;
			alloc_info.memoryTypeIndex = Util::FindMemoryType(mem_req.memoryTypeBits, Util::ToVkMemoryPropertyFlags(GPU_MEMORY_DEVICE_LOCAL));
			alloc_info.pNext = &alloc_flags;

			VkDeviceMemory vk_device_memory = VK_NULL_HANDLE;
			VkCheckResult(vkAllocateMemory(vk_inst.device, &alloc_info, nullptr, &vk_device_memory));
			VkCheckResult(vkBindImageMemory(vk_inst.device, image.vk_image, vk_device_memory, 0));

			Vulkan::DebugNameObject((uint64_t)vk_device_memory, VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT, texture_info.name);

			VulkanMemory memory = {};
			memory.vk_device_memory = vk_device_memory;
			memory.vk_memory_flags = Util::ToVkMemoryPropertyFlags(GPU_MEMORY_DEVICE_LOCAL);
			memory.vk_memory_index = alloc_info.memoryTypeIndex;

			return memory;
		}

		void Free(const VulkanMemory& memory)
		{
			if (!memory.vk_device_memory)
				return;

			vkFreeMemory(vk_inst.device, memory.vk_device_memory, nullptr);
		}

		void* Map(const VulkanMemory& memory, uint64_t size, uint64_t offset)
		{
			void* mapped_ptr = nullptr;
			VkCheckResult(vkMapMemory(vk_inst.device, memory.vk_device_memory, offset, size, 0, &mapped_ptr));

			return mapped_ptr;
		}

		void Unmap(const VulkanMemory& memory)
		{
			vkUnmapMemory(vk_inst.device, memory.vk_device_memory);
		}

	}

}