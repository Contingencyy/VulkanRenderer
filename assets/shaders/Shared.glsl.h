#pragma once

#ifdef __cplusplus

#define DECLARE_STRUCT(x) struct alignas(16) x

#include "Common.h"
typedef uint32_t uint;
typedef glm::vec3 vec3;
typedef glm::vec4 vec4;
typedef glm::mat4 mat4;

#else

#define DECLARE_STRUCT(x) struct x

#endif

const uint DESCRIPTOR_SET_UNIFORM_BUFFER = 0;
const uint DESCRIPTOR_SET_STORAGE_BUFFER = 1;
const uint DESCRIPTOR_SET_STORAGE_IMAGE = 2;
const uint DESCRIPTOR_SET_SAMPLED_IMAGE = 3;
const uint DESCRIPTOR_SET_SAMPLER = 4;

// Reserved descriptors
const uint RESERVED_DESCRIPTOR_UNIFORM_BUFFER_COUNT = 2;
const uint RESERVED_DESCRIPTOR_UNIFORM_BUFFER_CAMERA = 0;
const uint RESERVED_DESCRIPTOR_UNIFORM_BUFFER_LIGHT_SOURCES = 1;

const uint RESERVED_DESCRIPTOR_STORAGE_BUFFER_COUNT = 1;
const uint RESERVED_DESCRIPTOR_STORAGE_BUFFER_MATERIAL = 0;

const uint RESERVED_DESCRIPTOR_STORAGE_IMAGE_COUNT = 2;
const uint RESERVED_DESCRIPTOR_STORAGE_IMAGE_HDR = 0;
const uint RESERVED_DESCRIPTOR_STORAGE_IMAGE_SDR = 1;

const uint MAX_UNIQUE_MATERIALS = 1000;
const uint MAX_LIGHT_SOURCES = 100;

// Debug render view modes
//const uint DEBUG_VIEW_BASE_COLOR = 0;
//const uint DEBUG_VIEW_NORMALS = 1;
//const uint DEBUG_VIEW_METALLIC_ROUGHNESS = 2;
//const uint DEBUG_VIEW_CLEARCOAT_ALPHA = 3;
//const uint DEBUG_VIEW_CLEARCOAT_NORMALS = 4;
//const uint DEBUG_VIEW_CLEARCOAT_ROUGHNESS = 5;

DECLARE_STRUCT(CameraData)
{
	mat4 view;
	mat4 proj;
	vec4 view_pos;
};

DECLARE_STRUCT(MaterialData)
{
	vec4 base_color_factor;
	float metallic_factor;
	float roughness_factor;
	uint base_color_texture_index;
	uint normal_texture_index;
	uint metallic_roughness_texture_index;
	uint sampler_index;

	uint has_clearcoat;
	float clearcoat_alpha_factor;
	float clearcoat_roughness_factor;
	uint clearcoat_alpha_texture_index;
	uint clearcoat_normal_texture_index;
	uint clearcoat_roughness_texture_index;
};

DECLARE_STRUCT(PointlightData)
{
	vec3 position;
	float intensity;
	vec3 color;
};
