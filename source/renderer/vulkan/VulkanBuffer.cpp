#include "Precomp.h"
#include "renderer/vulkan/VulkanBuffer.h"
#include "renderer/vulkan/VulkanInstance.h"
#include "renderer/vulkan/VulkanAllocator.h"
#include "renderer/vulkan/VulkanResourceTracker.h"
#include "renderer/vulkan/VulkanUtils.h"

namespace Vulkan
{

	VulkanBuffer CreateBuffer(const BufferCreateInfo& buffer_info)
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
		buffer.size_in_bytes = buffer_info.size_in_bytes;
		buffer.memory = AllocateDeviceMemory(buffer, buffer_info);

		ResourceTracker::TrackBuffer(buffer);

		return buffer;
	}

	void DestroyBuffer(VulkanBuffer& buffer)
	{
		ResourceTracker::RemoveBuffer(buffer);
		vkDestroyBuffer(vk_inst.device, buffer.vk_buffer, nullptr);
	}

	void CopyBuffers(VulkanCommandBuffer& command_buffer, VulkanBuffer& src_buffer, uint64_t src_offset, VulkanBuffer& dst_buffer, uint64_t dst_offset, uint64_t num_bytes)
	{
		VkBufferCopy copy_region = {};
		copy_region.srcOffset = src_offset;
		copy_region.dstOffset = dst_offset;
		copy_region.size = num_bytes;

		vkCmdCopyBuffer(command_buffer.vk_command_buffer, src_buffer.vk_buffer, dst_buffer.vk_buffer, 1, &copy_region);
	}

}
