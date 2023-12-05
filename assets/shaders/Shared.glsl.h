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

const uint DESCRIPTOR_SET_UBO = 0;
const uint DESCRIPTOR_SET_STORAGE_BUFFER = 1;
const uint DESCRIPTOR_SET_STORAGE_IMAGE = 2;
const uint DESCRIPTOR_SET_SAMPLED_IMAGE = 3;
const uint DESCRIPTOR_SET_SAMPLER = 4;

// Reserved descriptors
const uint RESERVED_DESCRIPTOR_UBO_COUNT = 3;
const uint RESERVED_DESCRIPTOR_UBO_CAMERA = 0;
const uint RESERVED_DESCRIPTOR_UBO_LIGHTS = 1;
const uint RESERVED_DESCRIPTOR_UBO_MATERIALS = 2;
//const uint RESERVED_DESCRIPTOR_UBO_SETTINGS = 3;

const uint RESERVED_DESCRIPTOR_STORAGE_BUFFER_COUNT = 0;

const uint RESERVED_DESCRIPTOR_STORAGE_IMAGE_COUNT = 3;
const uint RESERVED_DESCRIPTOR_STORAGE_IMAGE_DEBUG = 0;
const uint RESERVED_DESCRIPTOR_STORAGE_IMAGE_HDR = 1;
const uint RESERVED_DESCRIPTOR_STORAGE_IMAGE_SDR = 2;

const uint MAX_UNIQUE_MATERIALS = 1000;
const uint MAX_LIGHT_SOURCES = 100;

// Debug render modes
const uint DEBUG_RENDER_MODE_NONE = 0;
const uint DEBUG_RENDER_MODE_BASE_COLOR = 1;
const uint DEBUG_RENDER_MODE_NORMAL = 2;
const uint DEBUG_RENDER_MODE_METALLIC_ROUGHNESS = 3;
const uint DEBUG_RENDER_MODE_CLEARCOAT_ALPHA = 4;
const uint DEBUG_RENDER_MODE_CLEARCOAT_NORMAL = 5;
const uint DEBUG_RENDER_MODE_CLEARCOAT_ROUGHNESS = 6;
const uint DEBUG_RENDER_MODE_IBL_INDIRECT_DIFFUSE = 7;
const uint DEBUG_RENDER_MODE_NUM_MODES = 8;

#ifdef __cplusplus

#include <vector>

const std::vector<const char*> DEBUG_RENDER_MODE_LABELS =
{
	"None", "Base color", "Normal", "Metallic roughness", "Clearcoat alpha", "Clearcoat normal", "Clearcoat roughness", "IBL indirect diffuse"
};

#endif

DECLARE_STRUCT(CameraData)
{
	mat4 view;
	mat4 proj;
	vec4 view_pos;
};

DECLARE_STRUCT(MaterialData)
{
	vec4 albedo_factor;
	float metallic_factor;
	float roughness_factor;
	uint albedo_texture_index;
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
