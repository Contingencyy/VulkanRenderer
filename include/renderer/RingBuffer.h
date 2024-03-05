#pragma once
#include "renderer/VulkanIncludes.h"

/*

	The RingBuffer class is used for uploading data to the GPU and for uniform buffer data

*/

class RingBuffer
{
public:
	static constexpr uint64_t RING_BUFFER_DEFAULT_BYTE_SIZE = VK_MB(512ull);

public:
	// Factory patterns will return a unique_ptr, which can be converted into a shared_ptr if necessary
	// Factories should never own the memory themselves, and we can't make assumptions of the memory ownership
	// that the caller has intended, so we will return a unique_ptr
	static std::unique_ptr<RingBuffer> Create(uint64_t byte_size = RING_BUFFER_DEFAULT_BYTE_SIZE);

public:
	explicit RingBuffer(uint64_t byte_size);
	~RingBuffer();

private:
	VkBuffer m_vk_buffer = VK_NULL_HANDLE;

	uint64_t m_byte_size = 0;
	uint8_t* m_ptr_begin = nullptr;
	uint8_t* m_ptr_end = nullptr;

};
