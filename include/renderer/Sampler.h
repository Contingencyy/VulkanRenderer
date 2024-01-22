#pragma once
#include "renderer/VulkanBackend.h"
#include "renderer/RenderTypes.h"

class Sampler
{
public:
	// Factory patterns will return a unique_ptr, which can be converted into a shared_ptr if necessary
	// Factories should never own the memory themselves, and we can't make assumptions of the memory ownership
	// of the callee, so we will return a unique_ptr
	static std::unique_ptr<Sampler> Create(const SamplerCreateInfo& create_info);

public:
	Sampler(const SamplerCreateInfo& create_info);
	~Sampler();

	uint32_t GetIndex() const;

	VkSampler GetVkSampler() const { return m_vk_sampler; }

private:
	VkSampler m_vk_sampler = VK_NULL_HANDLE;
	DescriptorAllocation m_descriptor;

	SamplerCreateInfo m_create_info;

};
