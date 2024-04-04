#version 460

#include "Common.glsl"

layout(set = DESCRIPTOR_SET_STORAGE_IMAGE, binding = 0, rgba16) uniform restrict readonly image2D g_inputs[];
layout(set = DESCRIPTOR_SET_STORAGE_IMAGE, binding = 0, rgba8) uniform restrict writeonly image2D g_outputs[];

layout(std140, push_constant) uniform constants
{
	layout(offset = 0) uint hdr_src_index;
	layout(offset = 4) uint sdr_dst_index;
} push_consts;

layout(local_size_x = 8, local_size_y = 8) in;

vec3 ApplyExposure(vec3 color, float exposure)
{
	return color * exposure;
}

vec3 ApplyGamma(vec3 color, float gamma)
{
	return pow(abs(color), vec3((1.0 / gamma)));
}

float Luminance(vec3 color)
{
	return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

vec3 ChangeLuminance(vec3 color, float luma_out)
{
	float luma_in = Luminance(color);
	return color * (luma_out / luma_in);
}

vec3 TonemapReinhardLumaWhite(vec3 color, float max_white)
{
	float luma_old = Luminance(color);
	float numerator = luma_old * (1.0 + (luma_old / (max_white * max_white)));
	float luma_new = numerator / (1.0 + luma_old);

	return ChangeLuminance(color, luma_new);
}

vec3 LinearToSRGB(vec3 linear, float gamma)
{
	bvec3 cutoff = lessThan(linear, vec3(0.0031308));
	vec3 higher = vec3(1.055) * pow(linear, vec3(1.0 / gamma)) - vec3(0.055);
	vec3 lower = linear * vec3(12.92);
	
	return mix(higher, lower, cutoff);
}

void main()
{
	const ivec2 texel_pos = ivec2(gl_GlobalInvocationID.xy);
	vec4 hdr_color = imageLoad(g_inputs[push_consts.hdr_src_index], texel_pos);
	vec3 final_color = hdr_color.xyz;

	if (settings.debug_render_mode == DEBUG_RENDER_MODE_NONE)
	{
		final_color = ApplyExposure(final_color.rgb, settings.postfx_exposure);
		final_color = TonemapReinhardLumaWhite(final_color.rgb, settings.postfx_max_white);
		final_color = LinearToSRGB(final_color, settings.postfx_gamma);
	}

	// Image store always takes in a vec4, but if the destination texture is e.g. format RG16,
	// then only the RG components will be written and the others ignored
	imageStore(g_outputs[push_consts.sdr_dst_index], texel_pos, vec4(final_color, hdr_color.a));
}
