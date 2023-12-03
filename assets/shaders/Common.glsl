#pragma once
#include "Shared.glsl.h"

#extension GL_EXT_samplerless_texture_functions : enable

const float PI = 3.1415926535897932384626433832795;
const float INV_PI = 1.0 / 3.1415926535897932384626433832795;
const float TWO_PI = 3.1415926535897932384626433832795 * 2.0;
const float HALF_PI = 3.1415926535897932384626433832795 * 0.5;

layout(set = DESCRIPTOR_SET_SAMPLED_IMAGE, binding = 0) uniform texture2D g_textures[];
layout(set = DESCRIPTOR_SET_SAMPLED_IMAGE, binding = 0) uniform textureCube g_cube_textures[];
layout(set = DESCRIPTOR_SET_SAMPLER, binding = 0) uniform sampler g_samplers[];
//layout(set = DESCRIPTOR_SET_SAMPLER, binding = 0) uniform samplerCube g_cube_samplers[];

/*

	Texture sampling functions

*/

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

	Others

*/

vec2 Hammersley2D(uint i, uint normal) 
{
	uint bits = (i << 16u) | (i >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	float rdi = float(bits) * 2.3283064365386963e-10;
	return vec2(float(i) /float(normal), rdi);
}

vec3 ImportanceSampleGGX(vec2 Xi, vec3 normal, float roughness)
{
	float a = roughness * roughness;

	float phi = 2.0 * PI * Xi.x;
	float cos_theta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
	float sin_theta = sqrt(1.0 - cos_theta * cos_theta);

	vec3 cartesian = vec3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);
	
	vec3 up = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
	vec3 tangent = normalize(cross(up, normal));
	vec3 bitangent = cross(normal, tangent);

	vec3 sample_vec = tangent * cartesian.x + bitangent * cartesian.y + normal * cartesian.z;
	return normalize(sample_vec);
}
