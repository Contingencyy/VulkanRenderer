#version 450
#extension GL_KHR_vulkan_glsl : enable
#extension GL_EXT_nonuniform_qualifier : enable

struct Material
{
	vec4 base_color_factor;
	uint base_color_tex_index;
};

layout(set = 0, binding = 0, std140) readonly buffer MaterialBuffer
{
	Material mat[1000];
} g_materials;

layout(set = 1, binding = 2) uniform sampler2D g_tex_samplers[];

layout(push_constant, std140) uniform constants
{
	layout(offset = 16) uint mat_index;
} push_constants;

layout(location = 0) in vec2 frag_tex_coord;

layout(location = 0) out vec4 out_color;

void main()
{
	Material material = g_materials.mat[push_constants.mat_index];
	out_color = texture(g_tex_samplers[material.base_color_tex_index], frag_tex_coord) * material.base_color_factor;
}
