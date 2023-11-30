#pragma once
#include "Common.h"
#include "vulkan/vulkan.h"

namespace Vulkan
{
	struct Buffer;
	struct ImageView;
}

class DescriptorAllocation
{
public:
	DescriptorAllocation() = default;
	DescriptorAllocation(VkDescriptorType type, uint32_t descriptor_offset, uint32_t num_descriptors, uint32_t descriptor_size, void* base_ptr);

	void* GetDescriptor(uint32_t offset = 0);
	uint32_t GetIndex(uint32_t offset = 0);

	void WriteDescriptor(const Vulkan::Buffer& buffer, VkDeviceSize size, uint32_t offset = 0);
	void WriteDescriptor(const Vulkan::ImageView& view, VkImageLayout layout, uint32_t offset = 0);
	void WriteDescriptor(const VkSampler sampler, uint32_t offset = 0);

	bool IsNull();

private:
	VkDescriptorType m_descriptor_type = VK_DESCRIPTOR_TYPE_MAX_ENUM;

	uint32_t m_descriptor_offset = 0;
	uint32_t m_num_descriptors = 0;
	uint32_t m_descriptor_size = 0;

	void* m_ptr = nullptr;

};
