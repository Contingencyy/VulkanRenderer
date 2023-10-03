#include "renderer/DescriptorBuffer.h"

DescriptorAllocation::DescriptorAllocation(uint32_t num_descriptors, uint32_t descriptor_size, void* base_ptr)
	: m_num_descriptors(num_descriptors), m_descriptor_size(descriptor_size), m_ptr(base_ptr)
{
}

void* DescriptorAllocation::GetDescriptor(uint32_t offset)
{
	VK_ASSERT(offset < m_num_descriptors && "Tried to retrieve a descriptor from descriptor allocation that is larger than the allocation");
	return (uint8_t*)m_ptr + offset * m_descriptor_size;
}

DescriptorBuffer::DescriptorBuffer(VkDescriptorSetLayout descriptor_set_layout, const std::unordered_map<DescriptorType, DescriptorRange>& descriptor_ranges)
	: m_descriptor_ranges(descriptor_ranges)
{
	VkDeviceSize buffer_size;
	vk_inst.pFunc.get_descriptor_set_layout_size_ext(vk_inst.device, descriptor_set_layout, &buffer_size);
	Vulkan::CreateDescriptorBuffer(buffer_size, m_buffer);
}

DescriptorBuffer::~DescriptorBuffer()
{
	Vulkan::DestroyBuffer(m_buffer);
}

DescriptorAllocation&& DescriptorBuffer::Allocate(DescriptorType type, uint32_t num_descriptors)
{
	VK_ASSERT(m_descriptor_ranges.find(type) != m_descriptor_ranges.end() &&
		"Tried to allocate descriptor type that is not supported by the descriptor buffer and its layout");

	DescriptorRange& range = m_descriptor_ranges.at(type);
	VK_ASSERT(range.num_descriptors - range.current_descriptor_offset >= num_descriptors &&
		"Descriptor buffer has too few descriptors left to satisfy the allocation");

	void* alloc_ptr = GetDescriptorPointer(type, range.current_descriptor_offset);
	range.current_descriptor_offset += num_descriptors;

	return DescriptorAllocation(num_descriptors, range.descriptor_size, alloc_ptr);
}

void DescriptorBuffer::Free(const DescriptorAllocation& allocation)
{
	// TODO: Freeing descriptor allocations
	// TODO: Descriptor blocks with varying sizes inside of each DescriptorRange to allocate from freed descriptors again
}

uint32_t DescriptorBuffer::GetBindingOffset(DescriptorType type) const
{
	VK_ASSERT(m_descriptor_ranges.find(type) != m_descriptor_ranges.end() &&
		"Tried to retrieve binding offset for descriptor type that is not supported by the descriptor buffer and its layout");

	return m_descriptor_ranges.at(type).binding;
}

VkBufferDeviceAddressInfo DescriptorBuffer::GetDeviceAddressInfo() const
{
	VkBufferDeviceAddressInfo address_info = {};
	address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	address_info.buffer = m_buffer.buffer;

	return address_info;
}

void* DescriptorBuffer::GetDescriptorPointer(DescriptorType type, uint32_t offset)
{
	const DescriptorRange& range = m_descriptor_ranges.at(type);
	return (uint8_t*)m_buffer.ptr + range.buffer_offset + offset * range.descriptor_size;
}
