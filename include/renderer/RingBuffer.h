#pragma once
#include "renderer/VulkanIncludes.h"

/*

	The RingBuffer class is used for uploading data to the GPU and for uniform buffer data

*/

class RingBuffer
{
public:
	static constexpr uint64_t RING_BUFFER_DEFAULT_BYTE_SIZE = VK_GB(1ull);
	static constexpr uint32_t RING_BUFFER_MAX_ALLOCATIONS = 1024;
	static constexpr uint16_t RING_BUFFER_ALLOC_DEFAULT_ALIGNMENT = 4;

public:
	struct Allocation
	{
		uint64_t byte_offset = 0;
		uint8_t* ptr_begin = nullptr;
		uint8_t* ptr_end = nullptr;

		void Write(uint64_t offset, uint64_t num_bytes, const void* data);
		VkBuffer GetHandle() const { return vk_buffer; }

	private:
		friend class RingBuffer;

		VkBuffer vk_buffer = VK_NULL_HANDLE;

	};

public:
	RingBuffer();
	RingBuffer(uint64_t byte_size);
	~RingBuffer();

	Allocation Allocate(uint64_t num_bytes, uint32_t frame_index, uint16_t align = RING_BUFFER_ALLOC_DEFAULT_ALIGNMENT);

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
	VkBuffer m_vk_buffer = VK_NULL_HANDLE;
	VkDeviceMemory m_vk_memory = VK_NULL_HANDLE;

	uint64_t m_byte_size = 0;
	uint8_t* m_ptr_begin = nullptr;
	uint8_t* m_ptr_at = nullptr;
	uint8_t* m_ptr_free_until = nullptr;
	uint8_t* m_ptr_end = nullptr;

	std::queue<InFlightAllocation> m_in_flight_allocations;

};
