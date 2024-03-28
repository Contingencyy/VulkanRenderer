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
			buffer_info.usage_flags = BUFFER_USAGE_COPY_DST | BUFFER_USAGE_VERTEX |
				VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
			buffer_info.memory_flags = GPU_MEMORY_DEVICE_LOCAL;
			buffer_info.name = name;

			return Create(buffer_info);
		}

		VulkanBuffer CreateIndex(uint64_t size_in_bytes, const std::string& name)
		{
			BufferCreateInfo buffer_info = {};
			buffer_info.size_in_bytes = size_in_bytes;
			buffer_info.usage_flags = BUFFER_USAGE_COPY_DST | BUFFER_USAGE_INDEX |
				VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
			buffer_info.memory_flags = GPU_MEMORY_DEVICE_LOCAL;
			buffer_info.name = name;

			return Create(buffer_info);
		}

		VulkanBuffer CreateAccelerationStructure(uint64_t size_in_bytes, const std::string& name)
		{
			BufferCreateInfo buffer_info = {};
			buffer_info.size_in_bytes = size_in_bytes;
			buffer_info.usage_flags = BUFFER_USAGE_RAYTRACING_ACCELERATION_STRUCTURE;
			buffer_info.memory_flags = GPU_MEMORY_DEVICE_LOCAL;
			buffer_info.name = name;

			return Create(buffer_info);
		}

		VulkanBuffer CreateAccelerationStructureScratch(uint64_t size_in_bytes, const std::string& name)
		{
			BufferCreateInfo buffer_info = {};
			buffer_info.size_in_bytes = size_in_bytes;
			buffer_info.usage_flags = BUFFER_USAGE_RAYTRACING_SCRATCH;
			buffer_info.memory_flags = GPU_MEMORY_DEVICE_LOCAL;
			buffer_info.name = name;

			return Create(buffer_info);
		}

		VulkanBuffer CreateAccelerationStructureInstances(uint64_t size_in_bytes, const std::string& name)
		{
			BufferCreateInfo buffer_info = {};
			buffer_info.size_in_bytes = size_in_bytes;
			buffer_info.usage_flags = BUFFER_USAGE_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_INPUT;
			buffer_info.memory_flags = GPU_MEMORY_HOST_VISIBLE | GPU_MEMORY_HOST_COHERENT;
			buffer_info.name = name;

			return Create(buffer_info);
		}

		VulkanBuffer Create(const BufferCreateInfo& buffer_info)
		{
			VkBufferUsageFlags vk_usage_flags = Vulkan::Util::ToVkBufferUsageFlags(buffer_info.usage_flags);

			VkBufferCreateInfo vk_buffer_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
			vk_buffer_info.size = buffer_info.size_in_bytes;
			vk_buffer_info.usage = vk_usage_flags;
			vk_buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			VkBuffer vk_buffer = VK_NULL_HANDLE;
			VkCheckResult(vkCreateBuffer(vk_inst.device, &vk_buffer_info, nullptr, &vk_buffer));
			Vulkan::DebugNameObject((uint64_t)vk_buffer, VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT, buffer_info.name);

			VulkanBuffer buffer = {};
			buffer.vk_buffer = vk_buffer;
			buffer.vk_usage_flags = vk_usage_flags;
			buffer.memory = DeviceMemory::Allocate(buffer, buffer_info);
			buffer.size_in_bytes = buffer_info.size_in_bytes;
			buffer.offset_in_bytes = 0;

			ResourceTracker::TrackBuffer(buffer);

			return buffer;
		}

		void Destroy(VulkanBuffer& buffer)
		{
			if (!buffer.vk_buffer)
				return;

			ResourceTracker::RemoveBuffer(buffer);

			if (buffer.vk_acceleration_structure)
				vk_inst.pFunc.raytracing.destroy_acceleration_structure(vk_inst.device, buffer.vk_acceleration_structure, nullptr);

			DeviceMemory::Free(buffer.memory);
			vkDestroyBuffer(vk_inst.device, buffer.vk_buffer, nullptr);

			buffer = {};
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
