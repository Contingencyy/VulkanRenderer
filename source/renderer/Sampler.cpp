#include "Precomp.h"
#include "renderer/Sampler.h"

std::unique_ptr<Sampler> Sampler::Create(const SamplerCreateInfo& create_info)
{
	return std::make_unique<Sampler>(create_info);
}

Sampler::Sampler(const SamplerCreateInfo& create_info)
{
	m_vk_sampler = Vulkan::CreateSampler(create_info);
	m_descriptor = Vulkan::AllocateDescriptors(VK_DESCRIPTOR_TYPE_SAMPLER);

	VkDescriptorGetInfoEXT descriptor_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT };
	descriptor_info.type = VK_DESCRIPTOR_TYPE_SAMPLER;
	descriptor_info.data.pSampler = &m_vk_sampler;
	m_descriptor.WriteDescriptor(descriptor_info);
}

Sampler::~Sampler()
{
	if (!m_descriptor.IsNull())
		Vulkan::FreeDescriptors(m_descriptor);

	Vulkan::DestroySampler(m_vk_sampler);
}

uint32_t Sampler::GetIndex() const
{
	return m_descriptor.GetIndex();
}
