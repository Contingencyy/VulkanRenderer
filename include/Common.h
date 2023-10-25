#pragma once

#include "Logger.h"

#define VK_KB(x) (x << 10)
#define VK_MB(x) (x << 20)
#define VK_GB(x) (x << 30)

#define VK_ALIGN_POW2(x, align) ((intptr_t)(x) + ((align) - 1) & (-(intptr_t)(align)))
#define VK_ALIGN_DOWN_POW2(x, align) ((intptr_t)(x) & (-(intptr_t)(align)))

#define VK_ASSERT(x) assert(x)
#define VK_EXCEPT(...) auto logged_msg = LOG_ERR(__VA_ARGS__); throw std::runtime_error(logged_msg)

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtx/matrix_decompose.hpp"

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
