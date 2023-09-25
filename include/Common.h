#pragma once

#include "Logger.h"

#define VK_ASSERT(x) assert(x)
#define VK_EXCEPT(...) auto logged_msg = LOG_ERR(__VA_ARGS__); throw std::runtime_error(logged_msg)

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
