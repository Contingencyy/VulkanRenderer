#version 460

#include "Common.glsl"

layout(std140, push_constant) uniform PushConsts
{
	layout(offset = 4) uint env_texture_index;
	layout(offset = 8) uint env_sampler_index;
} push;

layout(location = 0) in vec3 local_position;

layout(location = 0) out vec4 out_color;

void main()
{
	vec3 environment_color = SampleTextureCube(push.env_texture_index, push.env_sampler_index, local_position).rgb;
	out_color = vec4(environment_color, 1.0);
}
