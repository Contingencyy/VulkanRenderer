#pragma once
#include "Common.h"
#include "renderer/VulkanBackend.h"

#include <unordered_map>

enum DescriptorType
{
	DescriptorType_Uniform,
	DescriptorType_Storage,
	DescriptorType_CombinedImageSampler,
	DescriptorType_NumBindings
};

class DescriptorAllocation
{
public:
	DescriptorAllocation() = default;
	DescriptorAllocation(uint32_t num_descriptors, uint32_t descriptor_size, void* base_ptr);

	void* GetDescriptor(uint32_t offset = 0);

private:
	uint32_t m_num_descriptors = 0;
	uint32_t m_descriptor_size = 0;

	void* m_ptr = nullptr;

};

struct DescriptorRange
{
	uint32_t num_descriptors = 0;
	uint32_t descriptor_size = 0;
	uint32_t buffer_offset = 0;
	uint32_t binding = 0;
	uint32_t current_descriptor_offset = 0;
};

class DescriptorBuffer
{
public:
	DescriptorBuffer() = default;
	DescriptorBuffer(VkDescriptorSetLayout descriptor_set_layout, const std::unordered_map<DescriptorType, DescriptorRange>& descriptor_ranges);
	~DescriptorBuffer();

	DescriptorAllocation&& Allocate(DescriptorType type, uint32_t num_descriptors = 1);
	void Free(const DescriptorAllocation& allocation);

	uint32_t GetBindingOffset(DescriptorType type) const;
	VkBufferDeviceAddressInfo GetDeviceAddressInfo() const;

private:
	void* GetDescriptorPointer(DescriptorType type, uint32_t offset);

private:
	Vulkan::Buffer m_buffer;
	std::unordered_map<DescriptorType, DescriptorRange> m_descriptor_ranges;

};
