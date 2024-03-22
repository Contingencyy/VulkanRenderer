#include "Precomp.h"
#include "renderer/vulkan/VulkanBuffer.h"
#include "renderer/vulkan/VulkanInstance.h"
#include "renderer/vulkan/VulkanDeviceMemory.h"
#include "renderer/vulkan/VulkanResourceTracker.h"
#include "renderer/vulkan/VulkanUtils.h"

namespace Vulkan
{

	namespace Buffer
	{

		VulkanBuffer CreateVertex(uint64_t size_in_bytes, const std::string& name)
		{
			BufferCreateInfo buffer_info = {};
			buffer_info.size_in_bytes = size_in_bytes;
			buffer_info.usage_flags = BUFFER_USAGE_COPY_DST | BUFFER_USAGE_VERTEX;
			buffer_info.memory_flags = GPU_MEMORY_DEVICE_LOCAL;
			buffer_info.name = name;

			return Create(buffer_info);
		}

		VulkanBuffer CreateIndex(uint64_t size_in_bytes, const std::string& name)
		{
			BufferCreateInfo buffer_info = {};
			buffer_info.size_in_bytes = size_in_bytes;
			buffer_info.usage_flags = BUFFER_USAGE_COPY_DST | BUFFER_USAGE_INDEX;
			buffer_info.memory_flags = GPU_MEMORY_DEVICE_LOCAL;
			buffer_info.name = name;

			return Create(buffer_info);
		}

		VulkanBuffer Create(const BufferCreateInfo& buffer_info)
		{
			VkBufferCreateInfo vk_buffer_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
			vk_buffer_info.size = buffer_info.size_in_bytes;
			vk_buffer_info.usage = Vulkan::Util::ToVkBufferUsageFlags(buffer_info.usage_flags);
			vk_buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			VkBuffer vk_buffer = VK_NULL_HANDLE;
			VkCheckResult(vkCreateBuffer(vk_inst.device, &vk_buffer_info, nullptr, &vk_buffer));
			Vulkan::DebugNameObject((uint64_t)vk_buffer, VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT, buffer_info.name);

			VulkanBuffer buffer = {};
			buffer.vk_buffer = vk_buffer;
			buffer.vk_usage_flags = vk_buffer_info.usage;
			buffer.memory = DeviceMemory::Allocate(buffer, buffer_info);
			buffer.size_in_bytes = buffer_info.size_in_bytes;
			buffer.offset_in_bytes = 0;

			ResourceTracker::TrackBuffer(buffer);

			return buffer;
		}

		void Destroy(const VulkanBuffer& buffer)
		{
			if (!buffer.vk_buffer)
				return;

			ResourceTracker::RemoveBuffer(buffer);

			DeviceMemory::Free(buffer.memory);
			vkDestroyBuffer(vk_inst.device, buffer.vk_buffer, nullptr);
		}

		VkMemoryRequirements GetMemoryRequirements(const VulkanBuffer& buffer)
		{
			VkMemoryRequirements2 memory_req = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 };
			VkBufferMemoryRequirementsInfo2 buffer_memory_req = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2 };
			buffer_memory_req.buffer = buffer.vk_buffer;

			vkGetBufferMemoryRequirements2(vk_inst.device, &buffer_memory_req, &memory_req);
			return memory_req.memoryRequirements;
		}

	}

}
