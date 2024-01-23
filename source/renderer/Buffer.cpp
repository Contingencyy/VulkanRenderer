#include "Precomp.h"
#include "renderer/Buffer.h"

std::unique_ptr<Buffer> Buffer::Create(const BufferCreateInfo& create_info)
{
	return std::make_unique<Buffer>(create_info);
}

std::unique_ptr<Buffer> Buffer::CreateStaging(size_t size_in_bytes, const std::string& name)
{
	BufferCreateInfo buffer_info = {};
	buffer_info.usage_flags = BUFFER_USAGE_STAGING | BUFFER_USAGE_COPY_SRC;
	buffer_info.memory_flags = GPU_MEMORY_HOST_VISIBLE | GPU_MEMORY_HOST_COHERENT;
	buffer_info.size_in_bytes = size_in_bytes;
	buffer_info.name = name;

	return std::make_unique<Buffer>(buffer_info);
}

std::unique_ptr<Buffer> Buffer::CreateUniform(size_t size_in_bytes, const std::string& name)
{
	BufferCreateInfo buffer_info = {};
	buffer_info.usage_flags = BUFFER_USAGE_UNIFORM;
	buffer_info.memory_flags = GPU_MEMORY_HOST_VISIBLE | GPU_MEMORY_HOST_COHERENT;
	buffer_info.size_in_bytes = size_in_bytes;
	buffer_info.name = name;

	return std::make_unique<Buffer>(buffer_info);
}

std::unique_ptr<Buffer> Buffer::CreateVertex(size_t size_in_bytes, const std::string& name)
{
	BufferCreateInfo buffer_info = {};
	buffer_info.usage_flags = BUFFER_USAGE_VERTEX | BUFFER_USAGE_COPY_DST;
	buffer_info.memory_flags = GPU_MEMORY_DEVICE_LOCAL;
	buffer_info.size_in_bytes = size_in_bytes;
	buffer_info.name = name;

	return std::make_unique<Buffer>(buffer_info);
}

std::unique_ptr<Buffer> Buffer::CreateIndex(size_t size_in_bytes, const std::string& name)
{
	BufferCreateInfo buffer_info = {};
	buffer_info.usage_flags = BUFFER_USAGE_INDEX | BUFFER_USAGE_COPY_DST;
	buffer_info.memory_flags = GPU_MEMORY_DEVICE_LOCAL;
	buffer_info.size_in_bytes = size_in_bytes;
	buffer_info.name = name;

	return std::make_unique<Buffer>(buffer_info);
}

std::unique_ptr<Buffer> Buffer::CreateInstance(size_t size_in_bytes, const std::string& name)
{
	BufferCreateInfo buffer_info = {};
	buffer_info.usage_flags = BUFFER_USAGE_VERTEX;
	buffer_info.memory_flags = GPU_MEMORY_HOST_VISIBLE | GPU_MEMORY_HOST_COHERENT;
	buffer_info.size_in_bytes = size_in_bytes;
	buffer_info.name = name;

	return std::make_unique<Buffer>(buffer_info);
}

static VkBufferUsageFlags ToVkBufferUsageFlags(Flags usage_flags)
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
	if ((usage_flags & BUFFER_USAGE_READ_ONLY) || (usage_flags & BUFFER_USAGE_READ_WRITE))
		vk_usage_flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	if (usage_flags & BUFFER_USAGE_COPY_SRC)
		vk_usage_flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	if (usage_flags & BUFFER_USAGE_COPY_DST)
		vk_usage_flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	if (usage_flags & BUFFER_USAGE_UNIFORM)
		vk_usage_flags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

	return vk_usage_flags;
}

static VkMemoryPropertyFlags ToVkMemoryPropertyFlags(Flags memory_flags)
{
	VkMemoryPropertyFlags vk_mem_property_flags = 0;

	if (memory_flags & GPU_MEMORY_DEVICE_LOCAL)
		vk_mem_property_flags |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	if (memory_flags & GPU_MEMORY_HOST_VISIBLE)
		vk_mem_property_flags |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
	if (memory_flags & GPU_MEMORY_HOST_COHERENT)
		vk_mem_property_flags |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	return vk_mem_property_flags;
}

Buffer::Buffer(const BufferCreateInfo& create_info)
	: m_create_info(create_info)
{
	m_vk_buffer = Vulkan::CreateBuffer(create_info.size_in_bytes, ToVkBufferUsageFlags(create_info.usage_flags));
	m_vk_device_memory = Vulkan::AllocateDeviceMemory(m_vk_buffer, ToVkMemoryPropertyFlags(create_info.memory_flags));

	if (create_info.memory_flags & GPU_MEMORY_HOST_VISIBLE)
		m_mapped_ptr = Vulkan::MapMemory(m_vk_device_memory, m_create_info.size_in_bytes);

	Vulkan::DebugNameObject((uint64_t)m_vk_buffer, VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT, create_info.name.c_str());
	Vulkan::DebugNameObject((uint64_t)m_vk_device_memory, VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT, create_info.name.c_str());
}

Buffer::~Buffer()
{
	if (!m_descriptor.IsNull())
		Vulkan::FreeDescriptors(m_descriptor);

	Vulkan::FreeDeviceMemory(m_vk_device_memory);
	Vulkan::DestroyBuffer(m_vk_buffer);
}

void Buffer::Write(VkDeviceSize size, void* src_ptr, VkDeviceSize src_offset, VkDeviceSize dst_offset) const
{
	// NOTE: The driver may or may not have immediately copied this over to buffer memory (e.g. caching)
	// or writes to the buffer are not visible in the mapped memory yet.
	// To deal with this problem, you either have to use a memory heap that is host coherent (VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
	// or call vkFlushMappedMemoryRanges after writing to the mapped memory and then calling vkInvalidateMappedMemoryRanges before reading from it
	// The transfer of data to the GPU happens in the background and the specification states it is guaranteed to be complete as of the next call to vkQueueSubmit

	VK_ASSERT(m_mapped_ptr && "Tried to write into buffer that was not mapped to memory");
	memcpy(m_mapped_ptr + dst_offset, (uint8_t*)src_ptr + src_offset, size);
}

void Buffer::CopyFrom(VkCommandBuffer command_buffer, const Buffer& src_buffer,
	VkDeviceSize size, VkDeviceSize src_offset, VkDeviceSize dst_offset) const
{
	VkBufferCopy copy_region = {};
	copy_region.srcOffset = src_offset;
	copy_region.dstOffset = dst_offset;
	copy_region.size = size;
	vkCmdCopyBuffer(command_buffer, src_buffer.m_vk_buffer, m_vk_buffer, 1, &copy_region);
}

void Buffer::CopyFromImmediate(const Buffer& src_buffer,
	VkDeviceSize size, VkDeviceSize src_offset, VkDeviceSize dst_offset) const
{
	VkCommandBuffer command_buffer = Vulkan::BeginImmediateCommand();
	CopyFrom(command_buffer, src_buffer, size, src_offset, dst_offset);
	Vulkan::EndImmediateCommand(command_buffer);
}

void Buffer::WriteDescriptorInfo(uint32_t descriptor_align)
{
	VK_ASSERT((m_create_info.usage_flags & BUFFER_USAGE_UNIFORM) || (m_create_info.usage_flags & BUFFER_USAGE_READ_ONLY) || (m_create_info.usage_flags & BUFFER_USAGE_READ_WRITE) &&
		"Tried to write a descriptor for a buffer that has no usage flag for UNIFORM or STORAGE type descriptors");

	// Allocate and write descriptor
	if (m_create_info.usage_flags & BUFFER_USAGE_UNIFORM)
		m_descriptor = Vulkan::AllocateDescriptors(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, descriptor_align);
	else if ((m_create_info.usage_flags & BUFFER_USAGE_READ_ONLY) || (m_create_info.usage_flags & BUFFER_USAGE_READ_WRITE))
		m_descriptor = Vulkan::AllocateDescriptors(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

	if (!m_descriptor.IsNull())
	{
		VkBufferDeviceAddressInfo buffer_address_info = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
		buffer_address_info.buffer = m_vk_buffer;

		VkDescriptorAddressInfoEXT descriptor_address_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT };
		descriptor_address_info.address = vkGetBufferDeviceAddress(vk_inst.device, &buffer_address_info);
		descriptor_address_info.format = VK_FORMAT_UNDEFINED;
		descriptor_address_info.range = m_create_info.size_in_bytes;

		VkDescriptorGetInfoEXT descriptor_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT };
		descriptor_info.type = m_descriptor.GetType();

		if (descriptor_info.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
		{
			VK_ASSERT((m_create_info.usage_flags & BUFFER_USAGE_UNIFORM) &&
				"Tried to get descriptor info for a buffer for descriptor type UNIFORM, but buffer was created without uniform buffer usage flag");
			descriptor_info.data.pUniformBuffer = &descriptor_address_info;
		}
		else if (descriptor_info.type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
		{
			VK_ASSERT(((m_create_info.usage_flags & BUFFER_USAGE_READ_ONLY) || (m_create_info.usage_flags & BUFFER_USAGE_READ_WRITE)) &&
				"Tried to get descriptor info for a buffer for descriptor type STORAGE, but buffer was created without storage buffer usage flag");
			descriptor_info.data.pStorageBuffer = &descriptor_address_info;
		}

		m_descriptor.WriteDescriptor(descriptor_info);
	}
}
