#include "renderer/DescriptorBuffer.h"
#include "renderer/DescriptorAllocation.h"

DescriptorBuffer::DescriptorBuffer(VkDescriptorType type, uint32_t descriptor_size, uint32_t num_descriptors)
	: m_descriptor_type(type), m_descriptor_size(descriptor_size), m_num_descriptors(num_descriptors)
{
	// Create descriptor set layout
	VkDescriptorSetLayoutBinding bindings = {};
	bindings.binding = 0;
	bindings.descriptorType = m_descriptor_type;
	bindings.descriptorCount = num_descriptors;
	bindings.stageFlags = VK_SHADER_STAGE_ALL;

	VkDescriptorBindingFlags binding_flags = {};

	VkDescriptorSetLayoutBindingFlagsCreateInfo binding_info = {};
	binding_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
	binding_info.pBindingFlags = &binding_flags;
	binding_info.bindingCount = 1;

	VkDescriptorSetLayoutCreateInfo layout_info = {};
	layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layout_info.bindingCount = 1;
	layout_info.pBindings = &bindings;
	layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
	layout_info.pNext = &binding_info;

	VkCheckResult(vkCreateDescriptorSetLayout(vk_inst.device, &layout_info, nullptr, &m_layout));

	// Create underlying buffer
	VkDeviceSize buffer_size;
	vk_inst.pFunc.get_descriptor_set_layout_size_ext(vk_inst.device, m_layout, &buffer_size);
	Vulkan::CreateDescriptorBuffer(buffer_size, m_buffer);
}

DescriptorBuffer::~DescriptorBuffer()
{
	Vulkan::DestroyBuffer(m_buffer);
	vkDestroyDescriptorSetLayout(vk_inst.device, m_layout, nullptr);
}

DescriptorAllocation&& DescriptorBuffer::Allocate(uint32_t num_descriptors)
{
	VK_ASSERT(m_num_descriptors - m_current_descriptor_offset >= num_descriptors &&
		"Descriptor buffer has too few descriptors left to satisfy the allocation");

	void* alloc_ptr = GetDescriptorPointer(m_current_descriptor_offset);
	m_current_descriptor_offset += num_descriptors;

	return DescriptorAllocation(m_descriptor_type, num_descriptors, m_descriptor_size, alloc_ptr);
}

void DescriptorBuffer::Free(const DescriptorAllocation& allocation)
{
	// TODO: Freeing descriptor allocations
	// TODO: Descriptor blocks with varying sizes inside of each DescriptorRange to allocate from freed descriptors again
}

VkDescriptorBufferBindingInfoEXT DescriptorBuffer::GetBindingInfo() const
{
	VkBufferDeviceAddressInfo address_info = {};
	address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	address_info.buffer = m_buffer.buffer;

	VkDescriptorBufferBindingInfoEXT binding_info = {};
	binding_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
	binding_info.address = vkGetBufferDeviceAddress(vk_inst.device, &address_info);
	binding_info.usage = VK_BUFFER_USAGE_2_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT;

	return binding_info;
}

void* DescriptorBuffer::GetDescriptorPointer(uint32_t offset)
{
	return (uint8_t*)m_buffer.ptr + offset * m_descriptor_size;
}
