#version 450

#extension GL_KHR_vulkan_glsl : enable
#extension GL_EXT_nonuniform_qualifier : enable

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

layout(std140, push_constant) uniform constants
{
	layout(offset = 4) uint camera_ubo_index;
	layout(offset = 8) uint irradiance_cubemap_index;
	layout(offset = 12) uint light_ubo_index;
	layout(offset = 16) uint num_light_sources;
	layout(offset = 20) uint mat_index;
} push_consts;

layout(location = 0) in vec4 frag_pos;
layout(location = 1) in vec2 frag_tex_coord;
layout(location = 2) in vec3 frag_normal;
layout(location = 3) in vec3 frag_tangent;
layout(location = 4) in vec3 frag_bitangent;

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 debug_color;

vec3 FresnelSchlickRoughness(float u, vec3 f0, float roughness)
{
	return f0 + (max(vec3(1.0 - roughness), f0) - f0) * pow(clamp(1.0 - u, 0.0, 1.0), 5.0);
}

const vec3 falloff = vec3(1.0f, 0.007f, 0.0002f);

vec3 EvaluateRadianceAtFragment(vec3 view_dir, vec3 frag_to_light, vec3 H, float NoL, float LoH,
	vec3 frag_base_color, vec3 frag_normal, float metalness, float roughness,
	float clearcoat_alpha, vec3 clearcoat_normal, float clearcoat_roughness)
{
	vec3 radiance = vec3(0.0f);

	vec3 Fc = vec3(0.0f), brdf_clearcoat = vec3(0.0f);
	bool clearcoat = clearcoat_alpha > 0.0f;
	if (clearcoat)
	{
		EvaluateBRDFClearCoat(view_dir, frag_to_light, H, LoH, clearcoat_normal, clearcoat_roughness, Fc, brdf_clearcoat);
	}

	vec3 brdf_specular, brdf_diffuse;
	EvaluateBRDF(view_dir, H, NoL, LoH, frag_base_color, frag_normal, metalness, roughness, clearcoat, brdf_specular, brdf_diffuse);
	
	return (brdf_diffuse + brdf_specular) * (1.0f - clearcoat_alpha * Fc) + clearcoat_alpha * brdf_clearcoat;
	//return brdf_specular + brdf_diffuse;
}

vec3 CalculateLightingAtFragment(vec3 view_pos, vec3 view_dir, vec3 frag_base_color, vec3 frag_normal, float metalness, float roughness, float clearcoat_alpha, float clearcoat_roughness, vec3 clearcoat_normal)
{
	vec3 color = vec3(0.0f);

	// Direct light from light sources
	for (uint light_idx = 0; light_idx < push_consts.num_light_sources; ++light_idx)
	{
		PointlightData pointlight = g_light_sources[push_consts.light_ubo_index].pointlight[light_idx];
		vec3 light_color = pointlight.color * pointlight.intensity;
		
		vec3 frag_to_light = normalize(pointlight.position - frag_pos.xyz);
		vec3 dist_to_light = vec3(length(pointlight.position - frag_pos.xyz));

		vec3 attenuation = clamp(1.0f / (falloff.x + (falloff.y * dist_to_light) + (falloff.z * (dist_to_light * dist_to_light))), 0.0f, 1.0f);
		float NoL = clamp(dot(frag_normal, frag_to_light), 0.0f, 1.0f);
		vec3 irradiance = light_color * NoL * attenuation;
		
		vec3 H = normalize(view_dir + frag_to_light);
		float LoH = clamp(dot(frag_to_light, H), 0.0f, 1.0f);
		vec3 radiance = EvaluateRadianceAtFragment(
			view_dir, frag_to_light, H, NoL, LoH,
			frag_base_color, frag_normal, metalness, roughness,
			clearcoat_alpha, clearcoat_normal, clearcoat_roughness
		);
		
		color += radiance * irradiance;
	}

	// Image-based lighting, indirect diffuse
	// I believe this is currently incorrect, as it does not take into account the clearcoat layer, so need to revisit
	vec3 f0 = vec3(0.04);
	f0 = mix(f0, frag_base_color, metalness);

	vec3 kS = FresnelSchlickRoughness(max(dot(frag_normal, view_dir), 0.0), f0, roughness * roughness);
	vec3 kD = 1.0 - kS;
	vec3 indirect_diffuse = SampleTextureCube(push_consts.irradiance_cubemap_index, 0, frag_normal).rgb;
	indirect_diffuse *= kD * frag_base_color;

	color += indirect_diffuse;

	return color;
}

void main()
{
	MaterialData material = g_materials[RESERVED_DESCRIPTOR_STORAGE_BUFFER_MATERIAL].mat[push_consts.mat_index];
	vec4 base_color = SampleTexture(material.base_color_texture_index, material.sampler_index, frag_tex_coord) * material.base_color_factor;
	vec3 normal = SampleTexture(material.normal_texture_index, material.sampler_index, frag_tex_coord).rgb;
	vec2 metallic_roughness = SampleTexture(material.metallic_roughness_texture_index, material.sampler_index, frag_tex_coord).bg * vec2(material.metallic_factor, material.roughness_factor);

	// Create the rotation matrix to bring the sampled normal from tangent space to world space
	mat3 TBN = mat3(frag_tangent, frag_bitangent, frag_normal);
	normal = normal * 2.0f - 1.0f;
	normal = normalize(TBN * normal);

	vec3 view_pos = g_camera[push_consts.camera_ubo_index].cam.view_pos.xyz;
	vec3 view_dir = normalize(view_pos - frag_pos.xyz);
	
	float clearcoat_alpha = SampleTexture(material.clearcoat_alpha_texture_index, material.sampler_index, frag_tex_coord).r * material.clearcoat_alpha_factor;
	vec3 clearcoat_normal = SampleTexture(material.clearcoat_normal_texture_index, material.sampler_index, frag_tex_coord).rgb;
	float clearcoat_roughness = SampleTexture(material.clearcoat_roughness_texture_index, material.sampler_index, frag_tex_coord).g * material.clearcoat_roughness_factor;

	clearcoat_normal = clearcoat_normal * 2.0f - 1.0f;
	clearcoat_normal = normalize(TBN * clearcoat_normal);

	vec3 color = CalculateLightingAtFragment(
		view_pos, view_dir,
		base_color.xyz, normal, metallic_roughness.x, metallic_roughness.y,
		clearcoat_alpha, clearcoat_roughness, clearcoat_normal
	);
	
	out_color = vec4(color, 1.0);
}
