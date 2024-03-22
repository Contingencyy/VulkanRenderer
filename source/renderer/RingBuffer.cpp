#include "Precomp.h"
#include "renderer/RingBuffer.h"
#include "renderer/vulkan/VulkanBuffer.h"
#include "renderer/vulkan/VulkanDeviceMemory.h"
#include "renderer/vulkan/VulkanBackend.h"

RingBuffer::RingBuffer()
	: RingBuffer(RING_BUFFER_DEFAULT_BYTE_SIZE)
{
}

RingBuffer::RingBuffer(uint64_t byte_size)
{
	// Create vulkan buffer
	BufferCreateInfo buffer_info = {};
	buffer_info.size_in_bytes = byte_size;
	// Ring buffer is used for transferring data (STAGING), uniform buffers (UNIFORM), and instance buffers (VERTEX)
	buffer_info.usage_flags = BUFFER_USAGE_STAGING | BUFFER_USAGE_UNIFORM | BUFFER_USAGE_VERTEX;
	buffer_info.memory_flags = GPU_MEMORY_HOST_VISIBLE | GPU_MEMORY_HOST_COHERENT;
	buffer_info.name = "Ring Buffer";

	m_buffer = Vulkan::Buffer::Create(buffer_info);

	m_ptr_begin = reinterpret_cast<uint8_t*>(Vulkan::DeviceMemory::Map(m_buffer.memory, m_buffer.size_in_bytes, 0));
	m_ptr_at = m_ptr_begin;
	m_ptr_end = m_ptr_begin + m_buffer.size_in_bytes;
	m_ptr_free_until = m_ptr_end;
}

RingBuffer::~RingBuffer()
{
	Vulkan::DeviceMemory::Unmap(m_buffer.memory);
	Vulkan::Buffer::Destroy(m_buffer);
}

RingBuffer::Allocation RingBuffer::Allocate(uint64_t num_bytes, uint16_t align)
{
	VK_ASSERT(num_bytes <= m_buffer.size_in_bytes && "Tried to allocate more memory from the ring buffer than the total ring buffer size");

	// Align the current write pointer, and check if we need to wrap
	m_ptr_at = (uint8_t*)VK_ALIGN_POW2(m_ptr_at, align);
	if (m_ptr_at + num_bytes > m_ptr_end)
	{
		m_ptr_at = m_ptr_begin;
	}

	uint8_t* alloc_ptr_begin = m_ptr_at;
	uint8_t* alloc_ptr_end = alloc_ptr_begin + num_bytes;

	// Check if we have enough space now, or if we have reached the maximum amount of allocations
	uint64_t bytes_left = m_ptr_free_until - m_ptr_at;
	if (num_bytes > bytes_left ||
		m_in_flight_allocations.size() > RING_BUFFER_MAX_ALLOCATIONS)
	{
		Flush();

		bytes_left = m_ptr_free_until - m_ptr_at;
		VK_ASSERT(num_bytes <= bytes_left && "Flushing the ring buffer did not free up enough memory");
	}

	// Update the current write pointer
	m_ptr_at = alloc_ptr_end;

	// Create the actual return allocation
	RingBuffer::Allocation alloc = {};
	alloc.buffer.vk_buffer = m_buffer.vk_buffer;
	alloc.buffer.memory = m_buffer.memory;
	alloc.buffer.vk_usage_flags = m_buffer.vk_usage_flags;
	alloc.buffer.offset_in_bytes = static_cast<uint64_t>(alloc_ptr_begin - m_ptr_begin);
	alloc.buffer.size_in_bytes = num_bytes;
	alloc.ptr_begin = alloc_ptr_begin;
	alloc.ptr_end = alloc_ptr_end;

	// Add a new in-flight allocation
	m_in_flight_allocations.emplace(alloc, Vulkan::GetCurrentFrameIndex());

	return alloc;
}

void RingBuffer::Flush()
{
	// NOTE: Currently the ring buffer flushes every in-flight allocation it can in the hopes that it will free up enough memory
	// However, it could happen that an allocation will fail even after flushing, which should be indicated to the caller so that
	// it can take action accordingly
	uint32_t finished_frame_index = Vulkan::GetLastFinishedFrameIndex();

	while (m_in_flight_allocations.size() > 0)
	{
		const auto& in_flight_alloc = m_in_flight_allocations.front();

		if (finished_frame_index >= in_flight_alloc.frame_index)
		{
			m_ptr_free_until = in_flight_alloc.alloc.ptr_end;
			m_in_flight_allocations.pop();
		}
		else
		{
			break;
		}
	}
}

void RingBuffer::Allocation::WriteBuffer(uint64_t byte_offset, uint64_t num_bytes, const void* data)
{
	VK_ASSERT(data && "Tried to write invalid data to the ring buffer allocation");
	VK_ASSERT(ptr_begin + byte_offset + num_bytes <= ptr_end && "Tried to write out of bounds of the ring buffer allocation");

	// NOTE: The driver may or may not have immediately copied this over to buffer memory (e.g. caching)
	// or writes to the buffer are not visible in the mapped memory yet.
	// To deal with this problem, you either have to use a memory heap that is host coherent (VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
	// or call vkFlushMappedMemoryRanges after writing to the mapped memory and then calling vkInvalidateMappedMemoryRanges before reading from it
	// The transfer of data to the GPU happens in the background and the specification states it is guaranteed to be complete as of the next call to vkQueueSubmit
	memcpy(ptr_begin + byte_offset, data, num_bytes);
}
