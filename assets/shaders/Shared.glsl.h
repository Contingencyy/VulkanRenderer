#ifdef __cplusplus
#pragma once

#define DECLARE_STRUCT(x) struct alignas(16) x

#include "Precomp.h"
typedef uint32_t uint;
typedef glm::vec3 vec3;
typedef glm::vec4 vec4;
typedef glm::mat4 mat4;

#else

#define DECLARE_STRUCT(x) struct x

#endif

// Bindless descriptor set bindings
const uint DESCRIPTOR_SET_UBO = 0;
const uint DESCRIPTOR_SET_STORAGE_BUFFER = 1;
const uint DESCRIPTOR_SET_STORAGE_IMAGE = 2;
const uint DESCRIPTOR_SET_SAMPLED_IMAGE = 3;
const uint DESCRIPTOR_SET_SAMPLER = 4;
const uint DESCRIPTOR_SET_ACCELERATION_STRUCTURES = 5;

// Reserved descriptors
const uint RESERVED_DESCRIPTOR_UBO_COUNT = 4;
const uint RESERVED_DESCRIPTOR_UBO_SETTINGS = 0;
const uint RESERVED_DESCRIPTOR_UBO_CAMERA = 1;
const uint RESERVED_DESCRIPTOR_UBO_LIGHTS = 2;
const uint RESERVED_DESCRIPTOR_UBO_MATERIALS = 3;

const uint RESERVED_DESCRIPTOR_STORAGE_BUFFER_COUNT = 0;

const uint RESERVED_DESCRIPTOR_STORAGE_IMAGE_COUNT = 2;
const uint RESERVED_DESCRIPTOR_STORAGE_IMAGE_HDR = 0;
const uint RESERVED_DESCRIPTOR_STORAGE_IMAGE_SDR = 1;

// Max values
const uint MAX_UNIQUE_MATERIALS = 1000;
const uint MAX_LIGHT_SOURCES = 100;

// Debug render modes
const uint DEBUG_RENDER_MODE_NONE = 0;
const uint DEBUG_RENDER_MODE_ALBEDO = 1;
const uint DEBUG_RENDER_MODE_NORMAL = 2;
const uint DEBUG_RENDER_MODE_METALLIC_ROUGHNESS = 3;
const uint DEBUG_RENDER_MODE_CLEARCOAT_ALPHA = 4;
const uint DEBUG_RENDER_MODE_CLEARCOAT_NORMAL = 5;
const uint DEBUG_RENDER_MODE_CLEARCOAT_ROUGHNESS = 6;
const uint DEBUG_RENDER_MODE_IBL_INDIRECT_DIFFUSE = 7;
const uint DEBUG_RENDER_MODE_IBL_INDIRECT_SPECULAR = 8;
const uint DEBUG_RENDER_MODE_IBL_BRDF_LUT = 9;
const uint DEBUG_RENDER_MODE_NUM_MODES = 10;

#ifdef __cplusplus
#include <vector>
const std::vector<const char*> DEBUG_RENDER_MODE_LABELS =
{
	"None",
	"Albedo", "Normal", "Metallic roughness",
	"Clearcoat alpha", "Clearcoat normal", "Clearcoat roughness",
	"IBL indirect diffuse", "IBL indirect specular", "IBL BRDF LUT"
};
#endif

const uint DIFFUSE_BRDF_MODEL_LAMBERTIAN = 0;
const uint DIFFUSE_BRDF_MODEL_BURLEY = 1;
const uint DIFFUSE_BRDF_MODEL_OREN_NAYAR = 2;
const uint DIFFUSE_BRDF_MODEL_NUM_MODELS = 3;

#ifdef __cplusplus
const std::vector<const char*> DIFFUSE_BRDF_MODEL_LABELS =
{
	"Lambertian", "Burley", "Oren-Nayar"
};
#endif

DECLARE_STRUCT(RenderSettings)
{
	uint use_direct_light;
	uint use_multiscatter;

	uint use_pbr_squared_roughness;
	uint use_pbr_clearcoat;
	uint pbr_diffuse_brdf_model;

	uint use_ibl;
	uint use_ibl_clearcoat;
	uint use_ibl_multiscatter;

	float exposure;
	float gamma;

	uint debug_render_mode;
	uint white_furnace_test;
};

DECLARE_STRUCT(CameraData)
{
	mat4 view;
	mat4 proj;
	vec4 view_pos;
};

DECLARE_STRUCT(MaterialData)
{
	uint albedo_texture_index;
	uint normal_texture_index;
	uint metallic_roughness_texture_index;

	uint sampler_index;
	vec4 albedo_factor;
	float metallic_factor;
	float roughness_factor;

	uint has_clearcoat;
	uint clearcoat_alpha_texture_index;
	uint clearcoat_normal_texture_index;
	uint clearcoat_roughness_texture_index;

	float clearcoat_alpha_factor;
	float clearcoat_roughness_factor;
};

DECLARE_STRUCT(PointlightData)
{
	vec3 pos;
	float intensity;
	vec3 color;
};
