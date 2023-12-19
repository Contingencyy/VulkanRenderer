#include "renderer/Buffer.h"

static VkBufferUsageFlags ToVkBufferUsageFlags(BufferUsageFlags usage_flags)
{
	VkBufferUsageFlags vk_usage_flags = 0;

	if (usage_flags & BUFFER_USAGE_STAGING)
		vk_usage_flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	if (usage_flags & BUFFER_USAGE_UNIFORM)
		vk_usage_flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	if (usage_flags & BUFFER_USAGE_VERTEX)
		vk_usage_flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	if (usage_flags & BUFFER_USAGE_INDEX)
		vk_usage_flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	if (usage_flags & BUFFER_USAGE_DESCRIPTOR)
		vk_usage_flags |= VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT;

	if ((usage_flags & BUFFER_USAGE_UNIFORM) || (usage_flags & BUFFER_USAGE_DESCRIPTOR))
		vk_usage_flags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

	return vk_usage_flags;
}

static VkMemoryPropertyFlags ToVkMemoryPropertyFlags(BufferUsageFlags usage_flags)
{
	VkMemoryPropertyFlags vk_mem_property_flags = 0;

	if ((usage_flags & BUFFER_USAGE_VERTEX) || (usage_flags & BUFFER_USAGE_INDEX))
		vk_mem_property_flags |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	if ((usage_flags & BUFFER_USAGE_STAGING) || (usage_flags & BUFFER_USAGE_DESCRIPTOR) || (usage_flags & BUFFER_USAGE_UNIFORM))
		vk_mem_property_flags |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	if ((usage_flags & BUFFER_USAGE_DESCRIPTOR) || (usage_flags & BUFFER_USAGE_UNIFORM))
		vk_mem_property_flags |= VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

	return vk_mem_property_flags;
}

Buffer::Buffer(const BufferCreateInfo& create_info)
	: m_create_info(create_info)
{
	m_vk_buffer = Vulkan::CreateBuffer(create_info.size_in_bytes, ToVkBufferUsageFlags(create_info.usage_flags));
	m_vk_device_memory = Vulkan::AllocateDeviceMemory(m_vk_buffer, ToVkMemoryPropertyFlags(create_info.usage_flags));

	if ((create_info.usage_flags & BUFFER_USAGE_STAGING) || (create_info.usage_flags & BUFFER_USAGE_UNIFORM) || (create_info.usage_flags & BUFFER_USAGE_DESCRIPTOR))
		m_mapped_ptr = Vulkan::MapMemory(m_vk_device_memory, m_create_info.size_in_bytes);

#ifdef _DEBUG
	Vulkan::DebugNameObject((uint64_t)m_vk_buffer, VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT, create_info.name.c_str());
	Vulkan::DebugNameObject((uint64_t)m_vk_device_memory, VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT, create_info.name.c_str());
#endif
}

Buffer::~Buffer()
{
	Vulkan::FreeDeviceMemory(m_vk_device_memory);
	Vulkan::DestroyBuffer(m_vk_buffer);
}

/*
void WriteBuffer(void* dst_ptr, void* src_ptr, VkDeviceSize size)
{
	// NOTE: The driver may or may not have immediately copied this over to buffer memory (e.g. caching)
	// or writes to the buffer are not visible in the mapped memory yet.
	// To deal with this problem, you either have to use a memory heap that is host coherent (VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
	// or call vkFlushMappedMemoryRanges after writing to the mapped memory and then calling vkInvalidateMappedMemoryRanges before reading from it
	// The transfer of data to the GPU happens in the background and the specification states it is guaranteed to be complete as of the next call to vkQueueSubmit
	memcpy(dst_ptr, src_ptr, size);
}

void CopyBuffer(const Buffer& src_buffer, const Buffer& dst_buffer, VkDeviceSize size, VkDeviceSize src_offset, VkDeviceSize dst_offset)
{
	VkCommandBuffer command_buffer = Vulkan::BeginImmediateCommand();

	VkBufferCopy copy_region = {};
	copy_region.srcOffset = src_offset;
	copy_region.dstOffset = dst_offset;
	copy_region.size = size;
	vkCmdCopyBuffer(command_buffer, src_buffer.buffer, dst_buffer.buffer, 1, &copy_region);

	Vulkan::EndImmediateCommand(command_buffer);
}
*/