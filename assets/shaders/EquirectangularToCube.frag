#version 460

/*

	This fragment shader projects an equirectangular map onto a unit cube
	Used for processing HDR environment maps for image-based lighting into cubemaps

*/

#include "Common.glsl"

layout(std140, push_constant) uniform PushConsts
{
	layout(offset = 68) uint src_texture_index;
	layout(offset = 72) uint src_sampler_index;
} push;

layout(location = 0) in vec3 local_position;

layout(location = 0) out vec4 out_color;

const vec2 INV_ATAN = vec2(0.1591, 0.3183);
vec2 SampleSphericalMap(vec3 dir)
{
    vec2 uv = vec2(atan(dir.z, dir.x), asin(dir.y));
    uv *= INV_ATAN;
    uv += 0.5;

    return uv;
}

void main()
{
	vec2 uv = SampleSphericalMap(normalize(local_position));
    vec3 sampled_color = SampleTexture(push.src_texture_index, push.src_sampler_index, uv).rgb;

    out_color = vec4(sampled_color, 1.0);
}
