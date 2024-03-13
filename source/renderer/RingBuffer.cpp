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
	buffer_info.usage_flags = BUFFER_USAGE_STAGING | BUFFER_USAGE_UNIFORM;
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
	VK_ASSERT(num_bytes < m_buffer.size_in_bytes && "Tried to allocate more memory from the ring buffer than the total ring buffer size");
	
	uint8_t* alloc_ptr_begin = (uint8_t*)VK_ALIGN_POW2(m_ptr_at, align);
	uint8_t* alloc_ptr_end = alloc_ptr_begin + num_bytes;

	// If the allocation goes beyond the end pointer of the ring buffer, wrap around and allocate from the beginning
	if (alloc_ptr_end > m_ptr_end)
	{
		alloc_ptr_begin = m_ptr_begin;
		alloc_ptr_end = m_ptr_begin + num_bytes;
	}

	// If the oldest in-flight allocation and the current allocation overlap or the maximum amount of allocations have been reached,
	// we need to free some memory first
	bool flush_required = (alloc_ptr_end > m_ptr_free_until) ||
			(m_in_flight_allocations.size() > RING_BUFFER_MAX_ALLOCATIONS);
	
	if (flush_required)
	{
		Flush();
		VK_ASSERT(alloc_ptr_end <= m_ptr_free_until && "Flushing the ring buffer did not free up enough memory");
	}

	// Update the current write pointer
	m_ptr_at = alloc_ptr_end;

	// Create the actual return allocation
	RingBuffer::Allocation alloc = {};
	alloc.buffer = m_buffer;
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
	// NOTE: Flushing the ring buffer currently blocks, might be nice to change this later so that while loading
	// assets this could just fail and the caller that wants to allocate simply has to try again
	while (m_in_flight_allocations.size() > 0)
	{
		const auto& in_flight_alloc = m_in_flight_allocations.front();
		if (in_flight_alloc.frame_index <= Vulkan::GetLastFinishedFrameIndex())
		{
			m_ptr_free_until = in_flight_alloc.alloc.ptr_end;
			m_in_flight_allocations.pop();
		}
		else
		{
			VK_ASSERT(false && "Ring buffer flush did not sync properly, flushing should wait for all in-flight allocations to finish");
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
