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
	layout(offset = 12) uint prefiltered_cubemap_index;
	layout(offset = 16) uint num_prefiltered_mips;
	layout(offset = 20) uint brdf_lut_index;
	layout(offset = 24) uint light_ubo_index;
	layout(offset = 28) uint num_light_sources;
	layout(offset = 32) uint mat_index;
} push_consts;

layout(location = 0) in vec4 frag_pos;
layout(location = 1) in vec2 frag_tex_coord;
layout(location = 2) in vec3 frag_normal;
layout(location = 3) in vec3 frag_tangent;
layout(location = 4) in vec3 frag_bitangent;

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 debug_color;

const vec3 falloff = vec3(1.0f, 0.007f, 0.0002f);

vec3 RadianceAtFragment(vec3 V, vec3 N, vec3 world_pos,
	vec3 albedo, float metallic, float roughness)
{
	// Remapping of roughness to be visually more linear
	roughness = roughness * roughness;

	vec3 f0 = mix(vec3(0.04), albedo, metallic);
	vec3 Lo = vec3(0.0);
	
	// Evaluate direct lighting
	for (uint i = 0; i < push_consts.num_light_sources; ++i)
	{
		PointlightData pointlight = g_light_sources[push_consts.light_ubo_index].pointlight[i];
		vec3 light_color = pointlight.color * pointlight.intensity;

		vec3 L = normalize(pointlight.position - world_pos.xyz);
		vec3 dist_to_light = vec3(length(pointlight.position - world_pos.xyz));
		vec3 dist_attenuation = clamp(1.0 / (falloff.x + (falloff.y * dist_to_light) + (falloff.z * (dist_to_light * dist_to_light))), 0.0, 1.0);

		Lo += BRDF(L, light_color, V, N, f0, albedo, metallic, roughness) * dist_attenuation;
	}
	
	// Evaluate indirect lighting from HDR environment
	vec3 R = reflect(-V, N);

	vec2 brdf = SampleTexture(push_consts.brdf_lut_index, 0, vec2(max(dot(N, V), 0.0), roughness)).rg;
	vec3 reflection = SampleTextureCubeLod(push_consts.prefiltered_cubemap_index, 0, R, roughness * push_consts.num_prefiltered_mips).rgb;
	vec3 irradiance = SampleTextureCube(push_consts.irradiance_cubemap_index, 0, N).rgb;

	vec3 diffuse = irradiance * albedo;
	vec3 F = F_SchlickRoughness(max(dot(N, V), 0.0), f0, roughness);
	vec3 specular = reflection * (F * brdf.x + brdf.y);

	vec3 kD = 1.0 - F;
	kD *= 1.0 - metallic;
	vec3 ambient = (kD * diffuse + specular);
	Lo += ambient;

	return Lo;
}

vec3 RadianceAtFragmentClearCoat(vec3 V, vec3 N, vec3 world_pos,
	vec3 albedo, float metallic, float roughness,
	float coat_alpha, vec3 coat_normal, float coat_roughness)
{
	// Remapping of roughness to be visually more linear
	roughness = roughness * roughness;
	coat_roughness = clamp(coat_roughness, 0.089f, 1.0f);
	coat_roughness = coat_roughness * coat_roughness;

	vec3 f0 = mix(vec3(0.04), albedo, metallic);
	vec3 Lo = vec3(0.0);

	// Evaluate direct lighting
	for (uint i = 0; i < push_consts.num_light_sources; ++i)
	{
		PointlightData pointlight = g_light_sources[push_consts.light_ubo_index].pointlight[i];
		vec3 light_color = pointlight.color * pointlight.intensity;
		
		vec3 L = normalize(pointlight.position - world_pos.xyz);
		vec3 dist_to_light = vec3(length(pointlight.position - world_pos.xyz));
		vec3 dist_attenuation = clamp(1.0 / (falloff.x + (falloff.y * dist_to_light) + (falloff.z * (dist_to_light * dist_to_light))), 0.0, 1.0);

		Lo += BRDFClearCoat(L, light_color, V, N, f0, albedo, metallic, roughness, coat_alpha, coat_normal, coat_roughness) * dist_attenuation;
	}
	
	// Evaluate indirect lighting from HDR environment
	vec3 R = reflect(-V, N);

	vec2 brdf = SampleTexture(push_consts.brdf_lut_index, 0, vec2(max(dot(N, V), 0.0), roughness)).rg;
	vec3 reflection = SampleTextureCubeLod(push_consts.prefiltered_cubemap_index, 0, R, roughness * push_consts.num_prefiltered_mips).rgb;
	vec3 irradiance = SampleTextureCube(push_consts.irradiance_cubemap_index, 0, N).rgb;

	vec3 diffuse = irradiance * albedo;
	vec3 F = F_SchlickRoughness(max(dot(N, V), 0.0), f0, roughness);
	vec3 specular = reflection * (F * brdf.x + brdf.y);

	vec3 kD = 1.0 - F;
	kD *= 1.0 - metallic;
	vec3 ambient = (kD * diffuse + specular);
	Lo += ambient;

	return Lo;
}

//vec3 CalculateLightingAtFragment(vec3 view_pos, vec3 view_dir, vec3 base_color, vec3 normal, float metallic, float roughness, float clearcoat_alpha, float clearcoat_roughness, vec3 clearcoat_normal)
//{
//	// TODO: This probably should be moved into the BRDF function above, to account for the weight of diffuse and specular
//	vec3 f0 = vec3(0.04);
//	f0 = mix(f0, base_color, metallic);
//
//	float NoV = max(dot(normal, view_dir), 0.0);
//	vec3 kS = FresnelSchlickRoughness(NoV, f0, roughness);
//
//	// Image-based lighting, indirect diffuse
//	// NOTE: Might be incorrect, since this does not take into account the clearcoat layer
//	vec3 kD = 1.0 - kS;
//	kD *= 1.0 - metallic;
//
//	vec3 indirect_diffuse = SampleTextureCube(push_consts.irradiance_cubemap_index, 0, normal).rgb;
//	indirect_diffuse *= kD * base_color;
//
//	// Image-based lighting, specular
//	vec3 R = reflect(-view_dir, normal);
//	vec3 prefiltered = SampleTextureCubeLod(push_consts.prefiltered_cubemap_index, 0, R, roughness * push_consts.num_prefiltered_mips).rgb;
//
//	vec2 env_brdf = SampleTexture(push_consts.brdf_lut_index, 0, vec2(NoV, roughness)).rg;
//	vec3 indirect_specular = prefiltered * (kS * env_brdf.x + env_brdf.y);
//
//	color += (indirect_diffuse + indirect_specular);
//
//	return color;
//}

void main()
{
	MaterialData material = g_materials[RESERVED_DESCRIPTOR_STORAGE_BUFFER_MATERIAL].mat[push_consts.mat_index];
	vec3 albedo = SampleTexture(material.albedo_texture_index, material.sampler_index, frag_tex_coord).rgb * material.albedo_factor.rgb;
	vec3 normal = SampleTexture(material.normal_texture_index, material.sampler_index, frag_tex_coord).rgb;
	vec2 metallic_roughness = SampleTexture(material.metallic_roughness_texture_index, material.sampler_index, frag_tex_coord).bg * vec2(material.metallic_factor, material.roughness_factor);

	// Create the rotation matrix to bring the sampled normal from tangent space to world space
	mat3 TBN = mat3(frag_tangent, frag_bitangent, frag_normal);
	normal = normal * 2.0 - 1.0;
	normal = normalize(TBN * normal);

	vec3 view_pos = g_camera[push_consts.camera_ubo_index].cam.view_pos.xyz;
	vec3 view_dir = normalize(view_pos - frag_pos.xyz);

	vec3 color = vec3(0.0);
	
	if (material.has_clearcoat == 1)
	{
		float clearcoat_alpha = SampleTexture(material.clearcoat_alpha_texture_index, material.sampler_index, frag_tex_coord).r * material.clearcoat_alpha_factor;
		vec3 clearcoat_normal = SampleTexture(material.clearcoat_normal_texture_index, material.sampler_index, frag_tex_coord).rgb;
		float clearcoat_roughness = SampleTexture(material.clearcoat_roughness_texture_index, material.sampler_index, frag_tex_coord).g * material.clearcoat_roughness_factor;

		clearcoat_normal = clearcoat_normal * 2.0 - 1.0;
		clearcoat_normal = normalize(TBN * clearcoat_normal);

		color += RadianceAtFragmentClearCoat(
			view_dir, normal, frag_pos.xyz,
			albedo, metallic_roughness.x, metallic_roughness.y,
			clearcoat_alpha, clearcoat_normal, clearcoat_roughness
		);
	}
	else
	{
		color = RadianceAtFragment(
			view_dir, normal, frag_pos.xyz,
			albedo, metallic_roughness.x, metallic_roughness.y
		);
	}
	
	out_color = vec4(color, 1.0);
}
