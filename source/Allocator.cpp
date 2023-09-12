#include "Allocator.h"
#include <memory>

void* Allocator::Allocate(size_t size)
{
	void* mem = malloc(size);
	memset(mem, 0, size);
	return mem;
}

void Allocator::Release(void* ptr)
{
	free(ptr);
}
