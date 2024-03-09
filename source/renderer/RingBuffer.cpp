#include "Precomp.h"
#include "renderer/RingBuffer.h"
#include "renderer/VulkanBackend.h"

RingBuffer::RingBuffer()
	: RingBuffer(RING_BUFFER_DEFAULT_BYTE_SIZE)
{
}

RingBuffer::RingBuffer(uint64_t byte_size)
	: m_byte_size(byte_size)
{
	// Create vulkan buffer
	BufferCreateInfo buffer_info = {};
	buffer_info.size_in_bytes = m_byte_size;
	buffer_info.usage_flags = BUFFER_USAGE_STAGING | BUFFER_USAGE_UNIFORM;
	buffer_info.memory_flags = GPU_MEMORY_HOST_VISIBLE | GPU_MEMORY_HOST_COHERENT;
	buffer_info.name = "Ring Buffer";

	m_vk_buffer = Vulkan::CreateBuffer(buffer_info);
	m_vk_memory = Vulkan::AllocateDeviceMemory(m_vk_buffer, buffer_info);

	m_ptr_begin = Vulkan::MapMemory(m_vk_memory, m_byte_size, 0);
	m_ptr_at = m_ptr_begin;
	m_ptr_end = m_ptr_begin + m_byte_size;
	m_ptr_free_until = m_ptr_end;
}

RingBuffer::~RingBuffer()
{
	Vulkan::UnmapMemory(m_vk_memory);
	Vulkan::FreeDeviceMemory(m_vk_memory);
	Vulkan::DestroyBuffer(m_vk_buffer);
}

RingBuffer::Allocation RingBuffer::Allocate(uint64_t num_bytes, uint32_t frame_index, uint16_t align)
{
	VK_ASSERT(num_bytes < m_byte_size && "Tried to allocate more memory from the ring buffer than the total ring buffer size");
	
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
	alloc.vk_buffer = m_vk_buffer;
	alloc.byte_offset = static_cast<uint64_t>(alloc_ptr_begin - m_ptr_begin);
	alloc.ptr_begin = alloc_ptr_begin;
	alloc.ptr_end = alloc_ptr_end;

	// Add a new in-flight allocation
	m_in_flight_allocations.emplace(alloc, frame_index);

	return alloc;
}

void RingBuffer::Flush()
{
	// NOTE: Flushing the ring buffer currently blocks, might be nice to change this later so that while loading
	// assets this could just fail and the caller that wants to allocate simply has to try again
	while (m_in_flight_allocations.size() > 0)
	{
		const auto& in_flight_alloc = m_in_flight_allocations.front();
		if (in_flight_alloc.frame_index <= vk_inst.last_finished_frame)
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

void RingBuffer::Allocation::Write(uint64_t offset, uint64_t num_bytes, const void* data)
{
	VK_ASSERT(ptr_begin + offset < ptr_end && "Tried to write at an offset that exceeded the allocation size");
	memcpy(ptr_begin + offset, data, num_bytes);
}
