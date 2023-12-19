#pragma once
#include "renderer/VulkanBackend.h"
#include "renderer/RenderTypes.h"

class Buffer
{
public:
	Buffer(const BufferCreateInfo& create_info);
	~Buffer();

private:
	VkBuffer m_vk_buffer = VK_NULL_HANDLE;
	VkDeviceMemory m_vk_device_memory = VK_NULL_HANDLE;
	uint8_t* m_mapped_ptr = nullptr;

	BufferCreateInfo m_create_info;

};
