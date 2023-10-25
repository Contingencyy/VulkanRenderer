#version 450
#pragma once

#extension GL_KHR_vulkan_glsl : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "Shared.glsl.h"

layout(std140, push_constant) uniform constants
{
	layout(offset = 0) uint hdr_src_index;
	layout(offset = 4) uint sdr_dst_index;
} push_constants;

layout(set = DESCRIPTOR_SET_STORAGE_IMAGE, binding = 0, rgba16) uniform readonly image2D g_inputs[];
layout(set = DESCRIPTOR_SET_STORAGE_IMAGE, binding = 0, rgba8) uniform writeonly image2D g_outputs[];

layout (local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

vec3 ApplyExposure(vec3 color, float exposure)
{
	return color * exposure;
}

vec3 ApplyGamma(vec3 color, float gamma)
{
	return pow(abs(color), vec3((1.0f / gamma)));
}

float Luminance(vec3 color)
{
	return dot(color, vec3(0.2126f, 0.7152f, 0.0722f));
}

vec3 ChangeLuminance(vec3 color, float luma_out)
{
	float luma_in = Luminance(color);
	return color * (luma_out / luma_in);
}

vec3 TonemapReinhardLumaWhite(vec3 color, float max_white)
{
	float luma_old = Luminance(color);
	float numerator = luma_old * (1.0f + (luma_old / (max_white * max_white)));
	float luma_new = numerator / (1.0f + luma_old);

	return ChangeLuminance(color, luma_new);
}

vec3 LinearToSRGB(vec3 linear)
{
	bvec3 cutoff = lessThan(linear, vec3(0.0031308f));
	vec3 higher = vec3(1.055f) * pow(linear, vec3(1.0f / 2.4f)) - vec3(0.055f);
	vec3 lower = linear * vec3(12.92f);
	
	return mix(higher, lower, cutoff);
}

void main()
{
	ivec2 texel_pos = ivec2(gl_GlobalInvocationID.xy);

	vec4 hdr_color = imageLoad(g_inputs[push_constants.hdr_src_index], ivec2(texel_pos));
	vec3 tonemapped = ApplyExposure(hdr_color.rgb, 1.5f);
	tonemapped = TonemapReinhardLumaWhite(hdr_color.rgb, 100.0f);
	//tonemapped = ApplyGamma(tonemapped, 2.2f);
	tonemapped = LinearToSRGB(tonemapped);

	imageStore(g_outputs[push_constants.sdr_dst_index], texel_pos, vec4(tonemapped, hdr_color.a));
}
