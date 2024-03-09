#pragma once
#include "renderer/vulkan/VulkanTypes.h"

namespace Vulkan
{

	VulkanMemory AllocateDeviceMemory(VulkanBuffer vk_buffer, const BufferCreateInfo& buffer_info);
	VulkanMemory AllocateDeviceMemory(VulkanImage vk_image, const TextureCreateInfo& texture_info);
	void FreeDeviceMemory(VulkanMemory device_memory);

	void* MapMemory(VulkanMemory device_memory, uint64_t size, uint64_t offset);
	void UnmapMemory(VulkanMemory device_memory);

}
