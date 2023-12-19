#pragma once
#include "renderer/VulkanBackend.h"
#include "renderer/RenderTypes.h"

class Sampler
{
public:
	Sampler(const SamplerCreateInfo& create_info);
	~Sampler();

private:
	VkSampler m_vk_sampler = VK_NULL_HANDLE;

	SamplerCreateInfo m_create_info;

};
