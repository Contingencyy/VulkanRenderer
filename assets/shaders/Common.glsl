#extension GL_EXT_samplerless_texture_functions : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_ray_query : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_buffer_reference2 : enable

#include "Shared.glsl.h"

const float PI = 3.1415926535897932384626433832795;
const float INV_PI = 1.0 / 3.1415926535897932384626433832795;
const float TWO_PI = 3.1415926535897932384626433832795 * 2.0;
const float HALF_PI = 3.1415926535897932384626433832795 * 0.5;

/*

	Storage buffers and acceleration structures
	Functionality for vertex pulling

*/

layout(set = DESCRIPTOR_SET_STORAGE_BUFFER, binding = 0, std430) readonly buffer VertexSSBOs
{
	Vertex vertices[];
} g_vertex_ssbos[];

layout(set = DESCRIPTOR_SET_STORAGE_BUFFER, binding = 0, std430) readonly buffer InstanceSSBOs
{
	InstanceData instance_data[];
} g_instance_ssbos[];

layout(set = DESCRIPTOR_SET_ACCELERATION_STRUCTURES, binding = 0) uniform accelerationStructureEXT g_tlas_scene[];

vec3 GetVertexPos(uint buffer_index, uint vertex_index)
{
	Vertex vertex = g_vertex_ssbos[buffer_index].vertices[vertex_index];
	return vec3(vertex.pos[0], vertex.pos[1], vertex.pos[2]);
}

vec2 GetVertexTexCoord(uint buffer_index, uint vertex_index)
{
	Vertex vertex = g_vertex_ssbos[buffer_index].vertices[vertex_index];
	return vec2(vertex.tex_coord[0], vertex.tex_coord[1]);
}

vec3 GetVertexNormal(uint buffer_index, uint vertex_index)
{
	Vertex vertex = g_vertex_ssbos[buffer_index].vertices[vertex_index];
	return vec3(vertex.normal[0], vertex.normal[1], vertex.normal[2]);
}

vec4 GetVertexTangent(uint buffer_index, uint vertex_index)
{
	Vertex vertex = g_vertex_ssbos[buffer_index].vertices[vertex_index];
	return vec4(vertex.tangent[0], vertex.tangent[1], vertex.tangent[2], vertex.tangent[3]);
}

mat4 GetInstanceTransform(uint buffer_index, uint instance_index)
{
	InstanceData instance = g_instance_ssbos[buffer_index].instance_data[instance_index];
	return mat4(
		instance.transform[0][0], instance.transform[0][1], instance.transform[0][2], instance.transform[0][3],
		instance.transform[1][0], instance.transform[1][1], instance.transform[1][2], instance.transform[1][3],
		instance.transform[2][0], instance.transform[2][1], instance.transform[2][2], instance.transform[2][3],
		instance.transform[3][0], instance.transform[3][1], instance.transform[3][2], instance.transform[3][3]
	);
}

uint GetInstanceMaterialIndex(uint buffer_index, uint instance_index)
{
	InstanceData instance = g_instance_ssbos[buffer_index].instance_data[instance_index];
	return instance.material_index;
}

/*

	Textures and samplers
	Functionality for sampling from textures

*/

layout(set = DESCRIPTOR_SET_UBO, binding = RESERVED_DESCRIPTOR_UBO_SETTINGS) uniform SettingsUBO
{
	RenderSettings settings;
};

layout(set = DESCRIPTOR_SET_UBO, binding = RESERVED_DESCRIPTOR_UBO_CAMERA) uniform CameraUBO
{
	GPUCamera camera;
};

layout(set = DESCRIPTOR_SET_UBO, binding = RESERVED_DESCRIPTOR_UBO_LIGHTS, std140) uniform LightUBO
{
	uint num_area_lights;
	uint ltc1_index;
	uint ltc2_index;

	GPUAreaLight area_lights[MAX_AREA_LIGHTS];
};

layout(set = DESCRIPTOR_SET_UBO, binding = RESERVED_DESCRIPTOR_UBO_MATERIALS) uniform MaterialUBO
{
	GPUMaterial materials[MAX_UNIQUE_MATERIALS];
};

layout(set = DESCRIPTOR_SET_SAMPLED_IMAGE, binding = 0) uniform texture2D g_textures[];
layout(set = DESCRIPTOR_SET_SAMPLED_IMAGE, binding = 0) uniform textureCube g_cube_textures[];
layout(set = DESCRIPTOR_SET_SAMPLER, binding = 0) uniform sampler g_samplers[];
//layout(set = DESCRIPTOR_SET_SAMPLER, binding = 0) uniform samplerCube g_cube_samplers[];

ivec2 GetTextureDimensions(uint tex_idx)
{
	return textureSize(g_textures[tex_idx], 0);
}

vec4 SampleTexture(uint tex_idx, uint samp_idx, vec2 tex_coord)
{
	return texture(sampler2D(g_textures[tex_idx], g_samplers[samp_idx]), tex_coord);
}

vec4 SampleTextureLod(uint tex_idx, uint samp_idx, vec2 tex_coord, float lod)
{
	return textureLod(sampler2D(g_textures[tex_idx], g_samplers[samp_idx]), tex_coord, lod);
}

vec4 SampleTextureCube(uint tex_idx, uint samp_idx, vec3 samp_dir)
{
	return texture(samplerCube(g_cube_textures[tex_idx], g_samplers[samp_idx]), samp_dir);
}

vec4 SampleTextureCubeLod(uint tex_idx, uint samp_idx, vec3 samp_dir, float lod)
{
	return textureLod(samplerCube(g_cube_textures[tex_idx], g_samplers[samp_idx]), samp_dir, lod);
}

/*

	Extra Utility

*/

vec2 Hammersley2D(uint i, uint N) 
{
	uint bits = (i << 16u) | (i >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	float rdi = float(bits) * 2.3283064365386963e-10;
	return vec2(float(i) /float(N), rdi);
}

float random(vec2 co)
{
	float a = 12.9898;
	float b = 78.233;
	float c = 43758.5453;
	float dt= dot(co.xy ,vec2(a,b));
	float sn= mod(dt,3.14);
	return fract(sin(sn) * c);
}

vec3 ImportanceSampleGGX(vec2 Xi, float roughness, vec3 normal) 
{
	// Maps a 2D point to a hemisphere with spread based on roughness
	float alpha = roughness * roughness;
	float phi = 2.0 * PI * Xi.x + random(normal.xz) * 0.1;
	float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (alpha*alpha - 1.0) * Xi.y));
	float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
	vec3 H = vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);

	// Tangent space
	vec3 up = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
	vec3 tangentX = normalize(cross(up, normal));
	vec3 tangentY = normalize(cross(normal, tangentX));

	// Convert to world Space
	return normalize(tangentX * H.x + tangentY * H.y + normal * H.z);
}
