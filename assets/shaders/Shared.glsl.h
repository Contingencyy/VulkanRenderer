#ifdef __cplusplus
#pragma once

#define DECLARE_STRUCT(x) struct x
#define DECLARE_STRUCT_UBO(x) struct alignas(16) x

#include "Precomp.h"
typedef uint32_t uint;
typedef glm::vec2 vec2;
typedef glm::vec3 vec3;
typedef glm::vec4 vec4;
typedef glm::mat4 mat4;

#else

#define DECLARE_STRUCT(x) struct x
#define DECLARE_STRUCT_UBO(x) struct x

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
const uint MAX_AREA_LIGHTS = 100;

// Debug render modes
const uint DEBUG_RENDER_MODE_NONE = 0;
const uint DEBUG_RENDER_MODE_ALBEDO = DEBUG_RENDER_MODE_NONE + 1;
const uint DEBUG_RENDER_MODE_VERTEX_NORMAL = DEBUG_RENDER_MODE_ALBEDO + 1;
const uint DEBUG_RENDER_MODE_VERTEX_TANGENT = DEBUG_RENDER_MODE_VERTEX_NORMAL + 1;
const uint DEBUG_RENDER_MODE_VERTEX_BITANGENT = DEBUG_RENDER_MODE_VERTEX_TANGENT + 1;
const uint DEBUG_RENDER_MODE_WORLD_NORMAL = DEBUG_RENDER_MODE_VERTEX_BITANGENT + 1;
const uint DEBUG_RENDER_MODE_METALLIC_ROUGHNESS = DEBUG_RENDER_MODE_WORLD_NORMAL + 1;
const uint DEBUG_RENDER_MODE_CLEARCOAT_ALPHA = DEBUG_RENDER_MODE_METALLIC_ROUGHNESS + 1;
const uint DEBUG_RENDER_MODE_CLEARCOAT_NORMAL = DEBUG_RENDER_MODE_CLEARCOAT_ALPHA + 1;
const uint DEBUG_RENDER_MODE_CLEARCOAT_ROUGHNESS = DEBUG_RENDER_MODE_CLEARCOAT_NORMAL + 1;
const uint DEBUG_RENDER_MODE_DIRECT_DIFFUSE = DEBUG_RENDER_MODE_CLEARCOAT_ROUGHNESS + 1;
const uint DEBUG_RENDER_MODE_DIRECT_SPECULAR = DEBUG_RENDER_MODE_DIRECT_DIFFUSE + 1;
const uint DEBUG_RENDER_MODE_IBL_INDIRECT_DIFFUSE = DEBUG_RENDER_MODE_DIRECT_SPECULAR + 1;
const uint DEBUG_RENDER_MODE_IBL_INDIRECT_SPECULAR = DEBUG_RENDER_MODE_IBL_INDIRECT_DIFFUSE + 1;
const uint DEBUG_RENDER_MODE_IBL_BRDF_LUT = DEBUG_RENDER_MODE_IBL_INDIRECT_SPECULAR + 1;
const uint DEBUG_RENDER_MODE_NUM_MODES = DEBUG_RENDER_MODE_IBL_BRDF_LUT + 1;

#ifdef __cplusplus
#include <vector>
static constexpr std::array<const char*, DEBUG_RENDER_MODE_NUM_MODES> DEBUG_RENDER_MODE_LABELS =
{
	"None",
	"Albedo", "Vertex normal", "Vertex tangent", "Vertex bitangent", "World normal", "Metallic roughness",
	"Clearcoat alpha", "Clearcoat normal", "Clearcoat roughness",
	"Direct diffuse", "Direct specular",
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

DECLARE_STRUCT(Vertex)
{
	float pos[3];
	float tex_coord[2];
	float normal[3];
	float tangent[4];
};

DECLARE_STRUCT(InstanceData)
{
	float transform[4][4];
	uint material_index;
};

DECLARE_STRUCT_UBO(RenderSettings)
{
	uint use_direct_light;
	uint use_multiscatter;

	uint use_pbr_squared_roughness;
	uint use_pbr_clearcoat;
	uint pbr_diffuse_brdf_model;

	uint use_ibl;
	uint use_ibl_clearcoat;
	uint use_ibl_multiscatter;

	float postfx_exposure;
	float postfx_gamma;
	float postfx_max_white;

	uint debug_render_mode;
	uint white_furnace_test;
};

DECLARE_STRUCT_UBO(GPUCamera)
{
	mat4 view;
	mat4 proj;
	vec4 view_pos;
};

DECLARE_STRUCT_UBO(GPUMaterial)
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

	uint blackbody_radiator;
};

DECLARE_STRUCT_UBO(GPUAreaLight)
{
	vec3 vert0;
	float color_red;
	vec3 vert1;
	float color_green;
	vec3 vert2;
	float color_blue;
	vec3 vert3;
	float intensity;
	bool two_sided;
	uint texture_index;
};
