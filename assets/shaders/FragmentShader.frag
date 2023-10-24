#version 450
#pragma once

#extension GL_KHR_vulkan_glsl : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "Shared.glsl.h"
#include "BRDF.glsl"

layout(set = DESCRIPTOR_SET_UNIFORM_BUFFER, binding = 0) uniform Camera
{
	CameraData cam;
} g_camera[];

layout(set = DESCRIPTOR_SET_UNIFORM_BUFFER, binding = 0) uniform LightSources
{
	PointlightData pointlight[MAX_LIGHT_SOURCES];
} g_light_sources[];

layout(std140, set = DESCRIPTOR_SET_STORAGE_BUFFER, binding = 0) readonly buffer MaterialBuffer
{
	MaterialData mat[MAX_UNIQUE_MATERIALS];
} g_materials[];

layout(set = DESCRIPTOR_SET_SAMPLED_IMAGE, binding = 0) uniform texture2D g_textures[];
layout(set = DESCRIPTOR_SET_SAMPLER, binding = 0) uniform sampler g_samplers[];

layout(std140, push_constant) uniform constants
{
	layout(offset = 0) uint camera_ubo_index;
	layout(offset = 4) uint light_ubo_index;
	layout(offset = 8) uint num_light_sources;
	layout(offset = 12) uint mat_index;
} push_constants;

layout(location = 0) in vec4 frag_pos;
layout(location = 1) in vec2 frag_tex_coord;
layout(location = 2) in vec3 frag_normal;
layout(location = 3) in vec3 frag_tangent;
layout(location = 4) in vec3 frag_bitangent;

layout(location = 0) out vec4 out_color;

const vec3 falloff = vec3(1.0f, 0.007f, 0.0002f);

vec3 CalculateLightingAtFragment(vec3 view_pos, vec3 view_dir, vec3 frag_normal, vec3 base_color, vec2 metallic_roughness)
{
	vec3 color = vec3(0.0f);

	for (uint light_idx = 0; light_idx < push_constants.num_light_sources; ++light_idx)
	{
		PointlightData pointlight = g_light_sources[push_constants.light_ubo_index].pointlight[light_idx];
		vec3 light_color = pointlight.color * pointlight.intensity;
		
		vec3 frag_to_light = normalize(pointlight.position - frag_pos.xyz);
		vec3 dist_to_light = vec3(length(pointlight.position - frag_pos.xyz));
		
		vec3 attenuation = clamp(1.0f / (falloff.x + (falloff.y * dist_to_light) + (falloff.z * (dist_to_light * dist_to_light))), 0.0f, 1.0f);
		float NoL = clamp(dot(frag_normal, frag_to_light), 0.0f, 1.0f);
		vec3 irradiance = light_color * NoL * attenuation;

		vec3 brdf_specular, brdf_diffuse;
		EvaluateBRDF(view_dir, frag_to_light, frag_normal, base_color, metallic_roughness.x, metallic_roughness.y, brdf_specular, brdf_diffuse);

		color += brdf_specular * irradiance + brdf_diffuse * irradiance;
	}

	return color;
}

void main()
{
	MaterialData material = g_materials[RESERVED_DESCRIPTOR_STORAGE_BUFFER_MATERIAL].mat[push_constants.mat_index];
	vec4 base_color = texture(sampler2D(g_textures[material.base_color_texture_index], g_samplers[material.sampler_index]), frag_tex_coord) * material.base_color_factor;
	vec3 normal = texture(sampler2D(g_textures[material.normal_texture_index], g_samplers[material.sampler_index]), frag_tex_coord).rgb;
	vec2 metallic_roughness = texture(sampler2D(g_textures[material.metallic_roughness_texture_index], g_samplers[material.sampler_index]), frag_tex_coord).bg * vec2(material.metallic_factor, material.roughness_factor);

	// Create the rotation matrix to bring the sampled normal from tangent space to world space
	mat3 TBN = mat3(frag_tangent, frag_bitangent, frag_normal);
	normal = normal * 2.0f - 1.0f;
	normal = normalize(TBN * normal);

	vec3 view_pos = g_camera[push_constants.camera_ubo_index].cam.view_pos.xyz;
	vec3 view_dir = normalize(view_pos - frag_pos.xyz);
	
	vec3 color = CalculateLightingAtFragment(view_pos, view_dir, normal, base_color.xyz, metallic_roughness);

	out_color.xyz = color;
	out_color.a = 1.0f;
}
