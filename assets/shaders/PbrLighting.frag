#version 450

#extension GL_EXT_nonuniform_qualifier : enable

#include "BRDF.glsl"

layout(std140, push_constant) uniform constants
{
	layout(offset = 0) uint irradiance_cubemap_index;
	layout(offset = 4) uint irradiance_sampler_index;
	layout(offset = 8) uint prefiltered_cubemap_index;
	layout(offset = 12) uint prefiltered_sampler_index;
	layout(offset = 16) uint num_prefiltered_mips;
	layout(offset = 20) uint brdf_lut_index;
	layout(offset = 24) uint brdf_lut_sampler_index;
	layout(offset = 28) uint mat_index;
} push_consts;

layout(location = 0) in vec4 frag_pos;
layout(location = 1) in vec2 frag_tex_coord;
layout(location = 2) in vec3 frag_normal;
layout(location = 3) in vec3 frag_tangent;
layout(location = 4) in vec3 frag_bitangent;

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 debug_color;

const vec3 falloff = vec3(1.0f, 0.007f, 0.0002f);

struct ViewInfo
{
	vec3 pos;
	vec3 dir;
};

struct GeometryInfo
{
	vec3 pos_world;

	vec3 albedo;
	vec3 normal;
	float metallic;
	float roughness;

	bool has_coat;
	float alpha_coat;
	vec3 normal_coat;
	float roughness_coat;
};

vec3 EvaluateLighting(ViewInfo view, GeometryInfo geo)
{
	vec3 f0 = mix(vec3(0.04), geo.albedo, geo.metallic);
	vec3 Lo = vec3(0.0);

	// Direct lighting
	if (settings.use_direct_light == 1)
	{
		for (uint i = 0; i < num_pointlights; ++i)
		{
			PointlightData pointlight = pointlights[i];
			vec3 light_color = pointlight.color * pointlight.intensity;

			vec3 light_dir = normalize(pointlight.pos - geo.pos_world);
			vec3 dist_to_light = vec3(length(pointlight.pos - geo.pos_world));
			vec3 dist_attenuation = clamp(1.0 / (falloff.x + (falloff.y * dist_to_light) + (falloff.z * (dist_to_light * dist_to_light))), 0.0, 1.0);
			float NoL = clamp(dot(geo.normal, light_dir), 0.0, 1.0);
			
			vec3 Fc = vec3(0.0);
			vec3 brdf_clearcoat = vec3(0.0);

			if (geo.has_coat)
			{
				EvaluateBRDFClearCoat(light_dir, view.dir, geo.normal_coat, f0, geo.roughness_coat, Fc, brdf_clearcoat);
			}

			vec3 brdf_diffuse = vec3(0.0);
			vec3 brdf_specular = vec3(0.0);
			EvaluateBRDFBase(light_dir, view.dir, geo.normal, f0, geo.albedo, geo.metallic, geo.roughness, brdf_diffuse, brdf_specular);

			if (geo.has_coat)
			{
				Lo = (brdf_diffuse + brdf_specular) * (1.0 - geo.alpha_coat * Fc) + geo.alpha_coat * brdf_clearcoat;
			}
			else
			{
				Lo += (brdf_diffuse + brdf_specular) * light_color * NoL * dist_attenuation;
			}
		}
	}

	// Indirect lighting from HDR environment
	if (settings.use_ibl == 1)
	{
		vec3 R = reflect(-view.dir, geo.normal);

		vec2 env_brdf = SampleTexture(push_consts.brdf_lut_index, push_consts.brdf_lut_sampler_index, vec2(max(dot(geo.normal, view.dir), 0.0), geo.roughness)).rg;
		vec3 reflection = SampleTextureCubeLod(push_consts.prefiltered_cubemap_index, push_consts.prefiltered_sampler_index, R, geo.roughness * push_consts.num_prefiltered_mips).rgb;
		vec3 irradiance = SampleTextureCube(push_consts.irradiance_cubemap_index, push_consts.irradiance_sampler_index, geo.normal).rgb;

		// TODO: Pick between lambertian, Burley, and Oren-Nayar
		vec3 diffuse_color = geo.albedo * INV_PI;
		vec3 diffuse = irradiance * diffuse_color;

		vec3 F = F_SchlickRoughness(max(dot(geo.normal, view.dir), 0.0), f0, geo.roughness);
		vec3 FssEss = F * env_brdf.x + env_brdf.y;
		vec3 specular = reflection * FssEss;

		// Take into account clearcoat layer for IBL
		if (settings.use_ibl_specular_clearcoat == 1 && geo.has_coat)
		{
			vec3 Fc = F_SchlickRoughness(max(dot(geo.normal_coat, view.dir), 0.0), f0, geo.roughness_coat);
			// Take into account energy lost in the clearcoat layer by adjusting the base layer
			diffuse *= 1.0 - Fc;
			specular *= pow(1.0 - geo.alpha_coat * Fc, vec3(2.0));

			vec3 Rc = reflect(-view.dir, geo.normal_coat);

			vec3 reflection_coat = SampleTextureCubeLod(push_consts.prefiltered_cubemap_index, push_consts.prefiltered_sampler_index, Rc, geo.roughness_coat * push_consts.num_prefiltered_mips).rgb;
			vec2 env_brdf_coat = SampleTexture(push_consts.brdf_lut_index, push_consts.brdf_lut_sampler_index, vec2(max(dot(geo.normal_coat, view.dir), 0.0), geo.roughness_coat)).rg;
			specular += geo.alpha_coat * reflection_coat * (Fc * env_brdf_coat.x + env_brdf_coat.y);
		}

		vec3 kD = vec3(0.0);
		// Source (Fdez-Agüera): https://www.jcgt.org/published/0008/01/03/paper.pdf
		if (settings.use_ibl_specular_multiscatter == 1)
		{
			float Ems = (1.0 - (env_brdf.x + env_brdf.y));
			
			vec3 F_avg = f0 + (1.0 - f0) / 21.0;
			vec3 FmsEms = Ems * FssEss * F_avg / (1.0 - F_avg * Ems);
			kD = diffuse_color * (1.0 - FssEss - FmsEms);

			Lo += (FmsEms + kD) * irradiance + FssEss * reflection;
		}
		else
		{
			kD = (1.0 - F) * (1.0 - geo.metallic);
			Lo += kD * diffuse + specular;
		}

		if (settings.debug_render_mode == DEBUG_RENDER_MODE_IBL_INDIRECT_DIFFUSE)
		{
			Lo = kD * diffuse;
		}
		else if (settings.debug_render_mode == DEBUG_RENDER_MODE_IBL_INDIRECT_SPECULAR)
		{
			Lo = specular;
		}
		else if (settings.debug_render_mode == DEBUG_RENDER_MODE_IBL_BRDF_LUT)
		{
			Lo = vec3(env_brdf, 0.0);
		}
	}
	else
	{
		if (settings.debug_render_mode == DEBUG_RENDER_MODE_IBL_INDIRECT_DIFFUSE ||
			settings.debug_render_mode == DEBUG_RENDER_MODE_IBL_INDIRECT_SPECULAR ||
			settings.debug_render_mode == DEBUG_RENDER_MODE_IBL_BRDF_LUT)
		{
			Lo = vec3(0.0);
		}
	}

	return Lo;
}

void main()
{
	MaterialData material = materials[push_consts.mat_index];

	GeometryInfo geo_info;
	geo_info.has_coat = false;
	geo_info.alpha_coat = 0.0;
	geo_info.normal_coat = vec3(0.0);
	geo_info.roughness_coat = 0.0;

	// Sample default material values
	geo_info.pos_world = frag_pos.xyz;
	geo_info.albedo = SampleTexture(material.albedo_texture_index, material.sampler_index, frag_tex_coord).rgb * material.albedo_factor.rgb;
	vec3 sampled_normal = SampleTexture(material.normal_texture_index, material.sampler_index, frag_tex_coord).rgb;
	vec2 sampled_metallic_roughness = SampleTexture(material.metallic_roughness_texture_index, material.sampler_index, frag_tex_coord).bg * vec2(material.metallic_factor, material.roughness_factor);
	geo_info.metallic = sampled_metallic_roughness.x;
	geo_info.roughness = sampled_metallic_roughness.y;

	// Bring sampled normal from tangent to world space
	mat3 TBN = mat3(frag_tangent, frag_bitangent, frag_normal);
	sampled_normal = sampled_normal * 2.0 - 1.0;
	geo_info.normal = normalize(TBN * sampled_normal);

	if (settings.use_pbr_clearcoat == 1 && material.has_clearcoat == 1)
	{
		geo_info.has_coat = true;

		// Sample clearcoat material values
		geo_info.alpha_coat = SampleTexture(material.clearcoat_alpha_texture_index, material.sampler_index, frag_tex_coord).r * material.clearcoat_alpha_factor;
		vec3 sampled_normal_coat = SampleTexture(material.clearcoat_normal_texture_index, material.sampler_index, frag_tex_coord).rgb;
		geo_info.roughness_coat = SampleTexture(material.clearcoat_roughness_texture_index, material.sampler_index, frag_tex_coord).g * material.clearcoat_roughness_factor;
		
		// Bring sampled clearcoat normal from tangent to world space
		sampled_normal_coat = sampled_normal_coat * 2.0 - 1.0;
		geo_info.normal_coat = normalize(TBN * sampled_normal_coat);
	}

	// View info for lighting
	ViewInfo view_info;
	view_info.pos = camera.view_pos.xyz;
	view_info.dir = normalize(view_info.pos - frag_pos.xyz);

	// Square the roughness to be perceptually more linear, if the setting is enabled
	if (settings.use_pbr_squared_roughness == 1)
	{
		geo_info.roughness = clamp(geo_info.roughness, 0.089, 1.0);
		geo_info.roughness = pow(geo_info.roughness, 2.0);
		geo_info.roughness_coat = clamp(geo_info.roughness_coat, 0.089, 1.0);
		geo_info.roughness_coat = pow(geo_info.roughness_coat, 2.0);
	}

	// Write final color
	vec3 color = EvaluateLighting(view_info, geo_info);

	// Debug render modes
	switch (settings.debug_render_mode)
	{
		case DEBUG_RENDER_MODE_ALBEDO:
		{
			color = geo_info.albedo;
			break;
		}
		case DEBUG_RENDER_MODE_NORMAL:
		{
			color = geo_info.normal;
			break;
		}
		case DEBUG_RENDER_MODE_METALLIC_ROUGHNESS:
		{
			color = vec3(0.0, geo_info.metallic, geo_info.roughness);
			break;
		}
		case DEBUG_RENDER_MODE_CLEARCOAT_ALPHA:
		{
			if (material.has_clearcoat == 1)
				color = vec3(geo_info.alpha_coat);
			else
				color = vec3(1.0, 0.0, 1.0);
			break;
		}
		case DEBUG_RENDER_MODE_CLEARCOAT_NORMAL:
		{
			if (material.has_clearcoat == 1)
				color = geo_info.normal_coat;
			else
				color = vec3(1.0, 0.0, 1.0);
			break;
		}
		case DEBUG_RENDER_MODE_CLEARCOAT_ROUGHNESS:
		{
			if (material.has_clearcoat == 1)
				color = vec3(0.0, geo_info.roughness_coat, 0.0);
			else
				color = vec3(1.0, 0.0, 1.0);
			break;
		}
	}

	out_color = vec4(color, 1.0);
}
