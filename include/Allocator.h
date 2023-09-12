#pragma once

struct Allocator
{

	void* Allocate(size_t size);
	void Release(void* ptr);

};
