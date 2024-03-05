#include "Precomp.h"
#include "renderer/RingBuffer.h"
#include "renderer/VulkanBackend.h"

std::unique_ptr<RingBuffer> RingBuffer::Create(uint64_t byte_size)
{
	return std::make_unique<RingBuffer>(byte_size);
}

RingBuffer::RingBuffer(uint64_t byte_size)
	: m_byte_size(byte_size)
{
}

RingBuffer::~RingBuffer()
{
}
