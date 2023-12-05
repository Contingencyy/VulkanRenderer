#version 450

#extension GL_KHR_vulkan_glsl : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "Common.glsl"

layout(std140, push_constant) uniform constants
{
	layout(offset = 0) uint env_texture_index;
	layout(offset = 4) uint env_sampler_index;
} push_consts;

layout(location = 0) in vec3 local_position;

layout(location = 0) out vec4 out_color;

void main()
{
	vec3 environment_color = SampleTextureCube(push_consts.env_texture_index, push_consts.env_sampler_index, local_position).rgb;
	out_color = vec4(environment_color, 1.0);
}
