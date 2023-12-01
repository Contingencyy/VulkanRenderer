#pragma once
#include "Shared.glsl.h"

const float PI = 3.1415926535897932384626433832795;
const float INV_PI = 1.0 / 3.1415926535897932384626433832795;
const float TWO_PI = 3.1415926535897932384626433832795 * 2.0;
const float HALF_PI = 3.1415926535897932384626433832795 * 0.5;

layout(set = DESCRIPTOR_SET_SAMPLED_IMAGE, binding = 0) uniform texture2D g_textures[];
layout(set = DESCRIPTOR_SET_SAMPLED_IMAGE, binding = 0) uniform textureCube g_cube_textures[];
layout(set = DESCRIPTOR_SET_SAMPLER, binding = 0) uniform sampler g_samplers[];
//layout(set = DESCRIPTOR_SET_SAMPLER, binding = 0) uniform samplerCube g_cube_samplers[];

vec4 SampleTexture(uint tex_idx, uint samp_idx, vec2 tex_coord)
{
	return texture(sampler2D(g_textures[tex_idx], g_samplers[samp_idx]), tex_coord);
}

vec4 SampleTextureCube(uint tex_idx, uint samp_idx, vec3 samp_dir)
{
	return texture(samplerCube(g_cube_textures[tex_idx], g_samplers[samp_idx]), samp_dir);
}
