#pragma once
#include "renderer/VulkanBackend.h"

class DescriptorAllocation;

class DescriptorBuffer
{
public:
	static constexpr uint32_t DEFAULT_DESCRIPTOR_BUFFER_SIZE = 1000;

public:
	DescriptorBuffer(VkDescriptorType type, uint32_t descriptor_size, uint32_t num_descriptors = DEFAULT_DESCRIPTOR_BUFFER_SIZE);
	~DescriptorBuffer();

	DescriptorAllocation&& Allocate(uint32_t num_descriptors = 1);
	void Free(const DescriptorAllocation& allocation);

	VkDescriptorBufferBindingInfoEXT GetBindingInfo() const;
	VkDescriptorSetLayout GetDescriptorSetLayout() const { return m_layout; }

private:
	void* GetDescriptorPointer(uint32_t offset);

private:
	VkDescriptorType m_descriptor_type;
	VkDescriptorSetLayout m_layout;
	Vulkan::Buffer m_buffer;

	uint32_t m_num_descriptors = 0;
	uint32_t m_descriptor_size = 0;
	uint32_t m_current_descriptor_offset = 0;

};
