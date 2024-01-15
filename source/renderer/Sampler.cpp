#include "renderer/Sampler.h"

Sampler* Sampler::Create(const SamplerCreateInfo& create_info)
{
	return new Sampler(create_info);
}

void Sampler::Destroy(const Sampler* sampler)
{
	delete sampler;
}

static VkSamplerAddressMode ToVkAddressMode(SamplerAddressMode address_mode)
{
	switch (address_mode)
	{
	case SAMPLER_ADDRESS_MODE_REPEAT:
		return VK_SAMPLER_ADDRESS_MODE_REPEAT;
	case SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:
		return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	case SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:
		return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	case SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:
		return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	case SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE:
		return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
	}
}

static VkBorderColor ToVkBorderColor(SamplerBorderColor border_color)
{
	switch (border_color)
	{
	case SAMPLER_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK:
		return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
	case SAMPLER_BORDER_COLOR_INT_TRANSPARENT_BLACK:
		return VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
	case SAMPLER_BORDER_COLOR_FLOAT_OPAQUE_BLACK:
		return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
	case SAMPLER_BORDER_COLOR_INT_OPAQUE_BLACK:
		return VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	case SAMPLER_BORDER_COLOR_FLOAT_OPAQUE_WHITE:
		return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	case SAMPLER_BORDER_COLOR_INT_OPAQUE_WHITE:
		return VK_BORDER_COLOR_INT_OPAQUE_WHITE;
	}
}

static VkFilter ToVkFilter(SamplerFilter filter)
{
	switch (filter)
	{
	case SAMPLER_FILTER_NEAREST:
		return VK_FILTER_NEAREST;
	case SAMPLER_FILTER_LINEAR:
		return VK_FILTER_LINEAR;
	}
}

static VkSamplerMipmapMode ToVkSamplerMipmapMode(SamplerFilter filter)
{
	switch (filter)
	{
	case SAMPLER_FILTER_NEAREST:
		return VK_SAMPLER_MIPMAP_MODE_NEAREST;
	case SAMPLER_FILTER_LINEAR:
		return VK_SAMPLER_MIPMAP_MODE_LINEAR;
	}
}

static VkSamplerCreateInfo ToVkSamplerCreateInfo(const SamplerCreateInfo& create_info)
{
	VkSamplerCreateInfo vk_create_info = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };

	vk_create_info.addressModeU = ToVkAddressMode(create_info.address_u);
	vk_create_info.addressModeV = ToVkAddressMode(create_info.address_v);
	vk_create_info.addressModeW = ToVkAddressMode(create_info.address_w);
	vk_create_info.borderColor = ToVkBorderColor(create_info.border_color);

	vk_create_info.minFilter = ToVkFilter(create_info.filter_min);
	vk_create_info.magFilter = ToVkFilter(create_info.filter_mag);
	vk_create_info.mipmapMode = ToVkSamplerMipmapMode(create_info.filter_mip);

	vk_create_info.anisotropyEnable = static_cast<VkBool32>(create_info.enable_anisotropy);
	vk_create_info.maxAnisotropy = vk_inst.device_props.max_anisotropy;

	vk_create_info.minLod = create_info.min_lod;
	vk_create_info.maxLod = create_info.max_lod;

	return vk_create_info;
}

Sampler::Sampler(const SamplerCreateInfo& create_info)
{
	m_vk_sampler = Vulkan::CreateSampler(ToVkSamplerCreateInfo(create_info));
	m_descriptor = Vulkan::AllocateDescriptors(VK_DESCRIPTOR_TYPE_SAMPLER);

	VkDescriptorGetInfoEXT descriptor_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT };
	descriptor_info.type = VK_DESCRIPTOR_TYPE_SAMPLER;
	descriptor_info.data.pSampler = &m_vk_sampler;
	m_descriptor.WriteDescriptor(descriptor_info);
}

Sampler::~Sampler()
{
	Vulkan::FreeDescriptors(m_descriptor);
	Vulkan::DestroySampler(m_vk_sampler);
}
