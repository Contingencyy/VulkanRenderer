#pragma once
#include "Common.h"

#include <stdint.h>

struct ResourceHandle_t
{
	union
	{
		uint64_t value = ~0u;
		struct
		{
			uint32_t index;
			uint32_t version;
		};
	};
};

#define VK_RESOURCE_HANDLE_VALID(handle) (handle.index != ~0u && handle.version != ~0u)

struct Vertex
{
	glm::vec3 pos;
	glm::vec3 color;
	glm::vec2 tex_coord;
};
