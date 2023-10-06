#version 450
#extension GL_KHR_vulkan_glsl : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "Shared.glsl.h"
#include "BRDF.glsl"

layout(set = 0, binding = 0) uniform Camera
{
	CameraData cam;
} g_camera[];

layout(std140, set = 1, binding = 0) readonly buffer MaterialBuffer
{
	MaterialData mat[MAX_UNIQUE_MATERIALS];
} g_materials[];

layout(set = 2, binding = 0) uniform sampler2D g_tex_samplers[];

layout(std140, push_constant) uniform constants
{
	layout(offset = 0) uint camera_ubo_index;
	layout(offset = 4) uint mat_index;
} push_constants;

layout(location = 0) in vec4 frag_pos;
layout(location = 1) in vec2 frag_tex_coord;
layout(location = 2) in vec3 frag_normal;

layout(location = 0) out vec4 out_color;

void main()
{
	MaterialData material = g_materials[RESERVED_DESCRIPTOR_STORAGE_MATERIAL].mat[push_constants.mat_index];
	vec4 base_color = texture(g_tex_samplers[material.base_color_texture_index], frag_tex_coord) * material.base_color_factor;
	vec2 metallic_roughness = texture(g_tex_samplers[material.metallic_roughness_texture_index], frag_tex_coord).bg * vec2(material.metallic_factor, material.roughness_factor);

	// TEMP: Single point light
	vec3 light_pos = vec3(0.0f, 50.0f, 0.0f);
	vec3 light_color = vec3(200.0f, 200.0f, 200.0f);
	vec3 falloff = vec3(1.0f, 0.007f, 0.0002f);

	vec3 view_pos = g_camera[push_constants.camera_ubo_index].cam.view_pos.xyz;
	vec3 view_dir = normalize(view_pos - frag_pos.xyz);
	vec3 frag_to_light = normalize(light_pos - frag_pos.xyz);
	vec3 dist_to_light = vec3(length(light_pos - frag_pos.xyz));

	vec3 attenuation = clamp(1.0f / (falloff.x + (falloff.y * dist_to_light) + (falloff.z * (dist_to_light * dist_to_light))), 0.0f, 1.0f);
	vec3 radiance = attenuation * light_color;
	float NoL = clamp(dot(frag_normal, frag_to_light), 0.0f, 1.0f);

	vec3 brdf_specular, brdf_diffuse;
	EvaluateBRDF(view_dir, frag_to_light, frag_normal, base_color.xyz, metallic_roughness.x, metallic_roughness.y, brdf_specular, brdf_diffuse);

	vec3 irradiance = radiance * NoL;
	out_color.xyz = brdf_specular * irradiance + brdf_diffuse * irradiance;
	out_color.a = 1.0f;
}
