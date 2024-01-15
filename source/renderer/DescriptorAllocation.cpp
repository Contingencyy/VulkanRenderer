#include "renderer/DescriptorAllocation.h"
#include "renderer/VulkanBackend.h"
#include "renderer/Buffer.h"
#include "renderer/Texture.h"
#include "renderer/Sampler.h"

DescriptorAllocation::DescriptorAllocation(VkDescriptorType type, uint32_t descriptor_offset, uint32_t num_descriptors, uint32_t descriptor_size, uint8_t* base_ptr)
	: m_descriptor_type(type), m_descriptor_offset(descriptor_offset), m_num_descriptors(num_descriptors), m_descriptor_size(descriptor_size), m_ptr(base_ptr)
{
}

void DescriptorAllocation::WriteDescriptor(const VkDescriptorGetInfoEXT& descriptor_info, uint32_t offset)
{
	size_t descriptor_size = Vulkan::GetDescriptorTypeSize(m_descriptor_type);
	vk_inst.pFunc.get_descriptor_ext(vk_inst.device, &descriptor_info, descriptor_size, GetDescriptor(offset));
}

void* DescriptorAllocation::GetDescriptor(uint32_t offset) const
{
	VK_ASSERT(offset < m_num_descriptors && "Tried to retrieve a descriptor from descriptor allocation that is larger than the allocation");
	return m_ptr + offset * m_descriptor_size;
}

uint32_t DescriptorAllocation::GetIndex(uint32_t offset) const
{
	VK_ASSERT(!IsNull());
	return m_descriptor_offset + offset;
}

VkDescriptorType DescriptorAllocation::GetType() const
{
	return m_descriptor_type;
}

bool DescriptorAllocation::IsNull() const
{
	return (m_num_descriptors == 0 || m_descriptor_size == 0 || !m_ptr);
}