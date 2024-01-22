#pragma once
#include "renderer/VulkanBackend.h"
#include "renderer/RenderTypes.h"

class Buffer
{
public:
	// Factory patterns will return a unique_ptr, which can be converted into a shared_ptr if necessary
	// Factories should never own the memory themselves, and we can't make assumptions of the memory ownership
	// of the callee, so we will return a unique_ptr
	static std::unique_ptr<Buffer> Create(const BufferCreateInfo& create_info);
	static std::unique_ptr<Buffer> CreateStaging(size_t size_in_bytes, const std::string& name);
	static std::unique_ptr<Buffer> CreateUniform(size_t size_in_bytes, const std::string& name);
	static std::unique_ptr<Buffer> CreateVertex(size_t size_in_bytes, const std::string& name);
	static std::unique_ptr<Buffer> CreateIndex(size_t size_in_bytes, const std::string& name);
	static std::unique_ptr<Buffer> CreateInstance(size_t size_in_bytes, const std::string& name);

public:
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
