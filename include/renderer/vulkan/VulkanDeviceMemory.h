#pragma once
#include "renderer/vulkan/VulkanTypes.h"
#include "renderer/RenderTypes.h"

namespace Vulkan
{

	namespace DeviceMemory
	{

		VulkanMemory Allocate(const VulkanBuffer& vk_buffer, const BufferCreateInfo& buffer_info);
		VulkanMemory Allocate(const VulkanImage& vk_image, const TextureCreateInfo& texture_info);
		void Free(VulkanMemory& device_memory);

		void* Map(const VulkanMemory& device_memory, uint64_t size, uint64_t offset);
		void Unmap(const VulkanMemory& device_memory);

	}
	
}
