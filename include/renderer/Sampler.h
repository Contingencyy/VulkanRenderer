#pragma once
#include "renderer/VulkanBackend.h"
#include "renderer/RenderTypes.h"

class Sampler
{
public:
	static Sampler* Create(const SamplerCreateInfo& create_info);
	static void Destroy(const Sampler* sampler);

private:
	Sampler(const SamplerCreateInfo& create_info);
	~Sampler();

private:
	VkSampler m_vk_sampler = VK_NULL_HANDLE;
	DescriptorAllocation m_descriptor;

	SamplerCreateInfo m_create_info;

};
