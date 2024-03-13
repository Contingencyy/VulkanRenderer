#pragma once
#include "renderer/vulkan/VulkanTypes.h"

class Buffer;
class Sampler;

class DescriptorAllocation
{
public:
	DescriptorAllocation() = default;
	DescriptorAllocation(VkDescriptorType type, uint32_t descriptor_offset, uint32_t num_descriptors, uint32_t descriptor_size, uint8_t* base_ptr);

	void WriteDescriptor(const VkDescriptorGetInfoEXT& descriptor_info, uint32_t offset = 0);

	void* GetDescriptor(uint32_t offset = 0) const;
	uint32_t GetIndex(uint32_t offset = 0) const;
	VkDescriptorType GetType() const;

	bool IsNull() const;

private:
	VkDescriptorType m_descriptor_type = VK_DESCRIPTOR_TYPE_MAX_ENUM;

	uint32_t m_descriptor_offset = 0;
	uint32_t m_num_descriptors = 0;
	uint32_t m_descriptor_size = 0;

	uint8_t* m_ptr = nullptr;

};
