#version 450
#extension GL_KHR_vulkan_glsl : enable
#extension GL_EXT_nonuniform_qualifier : enable

layout(set = 0, binding = 2) uniform sampler2D g_tex_samplers[];

layout(push_constant, std140) uniform constants
{
	layout(offset = 4) uint texture_index;
} push_constants;

layout(location = 0) in vec2 frag_tex_coord;

layout(location = 0) out vec4 out_color;

void main()
{
	out_color = texture(g_tex_samplers[push_constants.texture_index], frag_tex_coord);
}
