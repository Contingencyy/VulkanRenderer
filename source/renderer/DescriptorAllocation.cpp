#include "renderer/DescriptorAllocation.h"
#include "renderer/VulkanBackend.h"

DescriptorAllocation::DescriptorAllocation(VkDescriptorType type, uint32_t descriptor_offset, uint32_t num_descriptors, uint32_t descriptor_size, void* base_ptr)
	: m_descriptor_type(type), m_descriptor_offset(descriptor_offset), m_num_descriptors(num_descriptors), m_descriptor_size(descriptor_size), m_ptr(base_ptr)
{
}

void* DescriptorAllocation::GetDescriptor(uint32_t offset)
{
	VK_ASSERT(offset < m_num_descriptors && "Tried to retrieve a descriptor from descriptor allocation that is larger than the allocation");
	return (uint8_t*)m_ptr + offset * m_descriptor_size;
}

uint32_t DescriptorAllocation::GetIndex(uint32_t offset)
{
	return m_descriptor_offset + offset;
}

void DescriptorAllocation::WriteDescriptor(const Vulkan::Buffer& buffer, VkDeviceSize size, uint32_t offset)
{
	VkBufferDeviceAddressInfoEXT buffer_address_info = {};
	buffer_address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	buffer_address_info.buffer = buffer.buffer;

	VkDescriptorAddressInfoEXT descriptor_address_info = {};
	descriptor_address_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT;
	descriptor_address_info.address = vkGetBufferDeviceAddress(vk_inst.device, &buffer_address_info);
	descriptor_address_info.format = VK_FORMAT_UNDEFINED;
	descriptor_address_info.range = size;

	VkDescriptorGetInfoEXT descriptor_info = {};
	descriptor_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;

	if (m_descriptor_type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
	{
		descriptor_info.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descriptor_info.data.pUniformBuffer = &descriptor_address_info;

		vk_inst.pFunc.get_descriptor_ext(vk_inst.device, &descriptor_info,
			vk_inst.descriptor_sizes.uniform_buffer, GetDescriptor(offset));
	}
	else if (m_descriptor_type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
	{
		descriptor_info.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		descriptor_info.data.pStorageBuffer = &descriptor_address_info;

		vk_inst.pFunc.get_descriptor_ext(vk_inst.device, &descriptor_info,
			vk_inst.descriptor_sizes.storage_buffer, GetDescriptor(offset));
	}
}

void DescriptorAllocation::WriteDescriptor(const Vulkan::ImageView& view, VkImageLayout layout, uint32_t offset)
{
	VkDescriptorImageInfo image_info = {};
	image_info.imageView = view.view;
	image_info.imageLayout = layout;
	image_info.sampler = VK_NULL_HANDLE;

	VkDescriptorGetInfoEXT descriptor_info = {};
	descriptor_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;

	if (m_descriptor_type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
	{
		descriptor_info.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		descriptor_info.data.pStorageImage = &image_info;
	}
	else if (m_descriptor_type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)
	{
		descriptor_info.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		descriptor_info.data.pSampledImage = &image_info;
	}

	vk_inst.pFunc.get_descriptor_ext(vk_inst.device, &descriptor_info,
		vk_inst.descriptor_sizes.sampled_image, GetDescriptor(offset));
}

void DescriptorAllocation::WriteDescriptor(const VkSampler sampler, uint32_t offset)
{
	VkDescriptorGetInfoEXT descriptor_info = {};
	descriptor_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
	descriptor_info.type = VK_DESCRIPTOR_TYPE_SAMPLER;
	descriptor_info.data.pSampler = &sampler;

	vk_inst.pFunc.get_descriptor_ext(vk_inst.device, &descriptor_info,
		vk_inst.descriptor_sizes.sampler, GetDescriptor(offset));
}

bool DescriptorAllocation::IsNull()
{
	return (m_num_descriptors == 0 || m_descriptor_size == 0 || !m_ptr);
}