#pragma once
#include "renderer/VulkanBackend.h"
#include "renderer/RenderTypes.h"

class Texture
{
public:
	Texture(const TextureCreateInfo& create_info);
	~Texture();

private:
	VkImage m_vk_image = VK_NULL_HANDLE;
	VkDeviceMemory m_vk_device_memory = VK_NULL_HANDLE;

	TextureCreateInfo m_create_info;

};
