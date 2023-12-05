#include "renderer/DescriptorBuffer.h"
#include "renderer/DescriptorAllocation.h"

#include "Shared.glsl.h"

DescriptorBuffer::DescriptorBuffer(VkDescriptorType type, uint32_t descriptor_size, uint32_t num_descriptors)
	: m_descriptor_type(type), m_descriptor_size(descriptor_size), m_num_descriptors(num_descriptors)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings;
	std::vector<VkDescriptorBindingFlags> binding_flags;

	if (m_descriptor_type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
	{
		bindings.resize(RESERVED_DESCRIPTOR_UBO_COUNT);
		for (uint32_t i = 0; i < bindings.size(); ++i)
		{
			bindings[i].binding = i;
			bindings[i].descriptorType = m_descriptor_type;
			bindings[i].descriptorCount = 1;
			bindings[i].stageFlags = VK_SHADER_STAGE_ALL;
		}
	}
	else
	{
		bindings.resize(1);
		bindings[0].binding = 0;
		bindings[0].descriptorType = m_descriptor_type;
		bindings[0].descriptorCount = num_descriptors;
		bindings[0].stageFlags = VK_SHADER_STAGE_ALL;
	}

	binding_flags.resize(bindings.size());

	VkDescriptorSetLayoutBindingFlagsCreateInfo binding_info = {};
	binding_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
	binding_info.bindingCount = (uint32_t)binding_flags.size();
	binding_info.pBindingFlags = binding_flags.data();

	VkDescriptorSetLayoutCreateInfo layout_info = {};
	layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layout_info.bindingCount = (uint32_t)bindings.size();
	layout_info.pBindings = bindings.data();
	layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
	layout_info.pNext = &binding_info;

	VkCheckResult(vkCreateDescriptorSetLayout(vk_inst.device, &layout_info, nullptr, &m_layout));

	// Create underlying buffer
	VkDeviceSize buffer_size;
	vk_inst.pFunc.get_descriptor_set_layout_size_ext(vk_inst.device, m_layout, &buffer_size);
	
	if (m_descriptor_type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
	{
		buffer_size *= VulkanInstance::MAX_FRAMES_IN_FLIGHT;
	}

	Vulkan::CreateDescriptorBuffer(buffer_size, m_buffer);
	m_current_ptr = m_buffer.ptr;
}

DescriptorBuffer::~DescriptorBuffer()
{
	Vulkan::DestroyBuffer(m_buffer);
	vkDestroyDescriptorSetLayout(vk_inst.device, m_layout, nullptr);
}

DescriptorAllocation DescriptorBuffer::Allocate(uint32_t num_descriptors, uint32_t align)
{
	VK_ASSERT(m_num_descriptors - m_current_descriptor_offset >= num_descriptors &&
		"Descriptor buffer has too few descriptors left to satisfy the allocation");

	uint8_t* alloc_ptr = m_current_ptr;
	if (align > 0)
	{
		alloc_ptr = (uint8_t*)VK_ALIGN_POW2(m_current_ptr, align);
	}
	m_current_ptr = alloc_ptr + num_descriptors * m_descriptor_size;

	DescriptorAllocation allocation(m_descriptor_type, m_current_descriptor_offset, num_descriptors, m_descriptor_size, alloc_ptr);
	m_current_descriptor_offset += num_descriptors;
	return allocation;
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
