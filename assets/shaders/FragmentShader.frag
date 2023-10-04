#version 450
#extension GL_KHR_vulkan_glsl : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "Shared.glsl.h"
#include "BRDF.glsl"

layout(std140, set = 0, binding = 0) readonly buffer MaterialBuffer
{
	MaterialData mat[MAX_UNIQUE_MATERIALS];
} g_materials;

layout(set = 1, binding = 2) uniform sampler2D g_tex_samplers[];

layout(std140, push_constant) uniform constants
{
	layout(offset = 16) uint mat_index;
} push_constants;

layout(location = 0) in vec2 frag_tex_coord;

layout(location = 0) out vec4 out_color;

void main()
{
	MaterialData material = g_materials.mat[push_constants.mat_index];
	out_color = texture(g_tex_samplers[material.base_color_texture_index], frag_tex_coord) * material.base_color_factor;
}
