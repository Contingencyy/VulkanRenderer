#pragma once
#include "Shared.glsl.h"

layout(set = DESCRIPTOR_SET_SAMPLED_IMAGE, binding = 0) uniform texture2D g_textures[];
layout(set = DESCRIPTOR_SET_SAMPLER, binding = 0) uniform sampler g_samplers[];

vec4 SampleTexture(uint tex_idx, uint samp_idx, vec2 tex_coord)
{
	return texture(sampler2D(g_textures[tex_idx], g_samplers[samp_idx]), tex_coord);
}
