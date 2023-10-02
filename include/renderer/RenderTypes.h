#pragma once
#include "Common.h"

using TextureHandle_t = ResourceHandle_t;
using MeshHandle_t = ResourceHandle_t;
using MaterialHandle_t = ResourceHandle_t;

struct Vertex
{
	glm::vec3 pos;
	glm::vec2 tex_coord;
};
