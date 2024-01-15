#pragma once
#include "renderer/VulkanBackend.h"
#include "renderer/RenderTypes.h"

class Buffer
{
public:
	static Buffer* CreateStaging(size_t size_in_bytes, const std::string& name);
	static Buffer* CreateUniform(size_t size_in_bytes, const std::string& name);
	static Buffer* CreateVertex(size_t size_in_bytes, const std::string& name);
	static Buffer* CreateIndex(size_t size_in_bytes, const std::string& name);

public:
	Buffer() = default;
	Buffer(const BufferCreateInfo& create_info);
	~Buffer();

public:
	void Write(VkDeviceSize size, void* src_ptr, VkDeviceSize src_offset = 0, VkDeviceSize dst_offset = 0) const;
	void CopyFrom(VkCommandBuffer command_buffer, const Buffer& src_buffer,
		VkDeviceSize size, VkDeviceSize src_offset, VkDeviceSize dst_offset) const;
	void CopyFromImmediate(const Buffer& src_buffer,
		VkDeviceSize size, VkDeviceSize src_offset, VkDeviceSize dst_offset) const;

	void WriteDescriptorInfo(uint32_t descriptor_align = 0);

	size_t GetSize() const { return m_create_info.size_in_bytes; }
	// NOTE: Should probably be removed, we currently need this for copying a buffer to a texture
	VkBuffer GetVkBuffer() const { return m_vk_buffer; }

private:
	VkBuffer m_vk_buffer = VK_NULL_HANDLE;
	VkDeviceMemory m_vk_device_memory = VK_NULL_HANDLE;
	uint8_t* m_mapped_ptr = nullptr;

	DescriptorAllocation m_descriptor;

	BufferCreateInfo m_create_info;

};
