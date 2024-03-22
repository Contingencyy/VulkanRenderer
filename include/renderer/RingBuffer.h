#pragma once
#include "renderer/vulkan/VulkanTypes.h"

/*

	The RingBuffer class is used for uploading data to the GPU and for uniform buffer data

*/

class RingBuffer
{
public:
	static constexpr uint64_t RING_BUFFER_DEFAULT_BYTE_SIZE = VK_MB(512ull);
	static constexpr uint32_t RING_BUFFER_MAX_ALLOCATIONS = 1024u;
	static constexpr uint16_t RING_BUFFER_ALLOC_DEFAULT_ALIGNMENT = 16;

public:
	struct Allocation
	{
		VulkanBuffer buffer;

		uint8_t* ptr_begin = nullptr;
		uint8_t* ptr_end = nullptr;

		void WriteBuffer(uint64_t byte_offset, uint64_t num_bytes, const void* data);
	};

public:
	RingBuffer();
	RingBuffer(uint64_t byte_size);
	~RingBuffer();

	Allocation Allocate(uint64_t num_bytes, uint16_t align = RING_BUFFER_ALLOC_DEFAULT_ALIGNMENT);

private:
	struct InFlightAllocation
	{
		Allocation alloc;
		uint32_t frame_index = 0;
	};

private:
	void Flush();

private:
	VulkanBuffer m_buffer;

	uint8_t* m_ptr_begin = nullptr;
	uint8_t* m_ptr_at = nullptr;
	uint8_t* m_ptr_free_until = nullptr;
	uint8_t* m_ptr_end = nullptr;

	std::queue<InFlightAllocation> m_in_flight_allocations;

};
