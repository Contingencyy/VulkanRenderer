#version 450

#extension GL_EXT_nonuniform_qualifier : enable

#include "BRDF.glsl"

layout(set = DESCRIPTOR_SET_UBO, binding = RESERVED_DESCRIPTOR_UBO_SETTINGS) uniform SettingsUBO
{
	RenderSettings settings;
};

layout(set = DESCRIPTOR_SET_UBO, binding = RESERVED_DESCRIPTOR_UBO_CAMERA) uniform CameraUBO
{
	CameraData camera;
};

layout(set = DESCRIPTOR_SET_UBO, binding = RESERVED_DESCRIPTOR_UBO_LIGHTS) uniform LightUBO
{
	PointlightData pointlights[MAX_LIGHT_SOURCES];
	uint num_pointlights;
};

layout(set = DESCRIPTOR_SET_UBO, binding = RESERVED_DESCRIPTOR_UBO_MATERIALS) uniform MaterialUBO
{
	MaterialData materials[MAX_UNIQUE_MATERIALS];
};

layout(std140, push_constant) uniform constants
{
	layout(offset = 0) uint irradiance_cubemap_index;
	layout(offset = 4) uint prefiltered_cubemap_index;
	layout(offset = 8) uint num_prefiltered_mips;
	layout(offset = 12) uint brdf_lut_index;
	layout(offset = 16) uint mat_index;
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
	if (settings.use_squared_roughness == 1)
	{
		roughness = roughness * roughness;
	}

	vec3 f0 = mix(vec3(0.04), albedo, metallic);
	vec3 Lo = vec3(0.0);
	
	// Evaluate direct lighting
	if (settings.use_direct_light == 1)
	{
		for (uint i = 0; i < num_pointlights; ++i)
		{
			PointlightData pointlight = pointlights[i];
			vec3 light_color = pointlight.color * pointlight.intensity;

			vec3 L = normalize(pointlight.position - world_pos.xyz);
			vec3 dist_to_light = vec3(length(pointlight.position - world_pos.xyz));
			vec3 dist_attenuation = clamp(1.0 / (falloff.x + (falloff.y * dist_to_light) + (falloff.z * (dist_to_light * dist_to_light))), 0.0, 1.0);

			Lo += BRDF(L, light_color, V, N, f0, albedo, metallic, roughness) * dist_attenuation;
		}
	}
	
	// Evaluate indirect lighting from HDR environment
	if (settings.use_ibl == 1)
	{
		vec3 R = reflect(-V, N);

		vec2 env_brdf = SampleTexture(push_consts.brdf_lut_index, 0, vec2(max(dot(N, V), 0.0), roughness)).rg;
		vec3 reflection = SampleTextureCubeLod(push_consts.prefiltered_cubemap_index, 0, R, roughness * push_consts.num_prefiltered_mips).rgb;
		vec3 irradiance = SampleTextureCube(push_consts.irradiance_cubemap_index, 0, N).rgb;

		vec3 diffuse = irradiance * albedo * INV_PI;
	
		vec3 F = F_SchlickRoughness(max(dot(N, V), 0.0), f0, roughness);
		vec3 specular = reflection * (F * env_brdf.x + env_brdf.y);
	
		vec3 kD = (1.0 - F) * (1.0 - metallic);
		vec3 ambient = (kD * diffuse + specular);
		Lo += ambient;

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

vec3 RadianceAtFragmentClearCoat(vec3 V, vec3 N, vec3 world_pos,
	vec3 albedo, float metallic, float roughness,
	float coat_alpha, vec3 coat_normal, float coat_roughness)
{
	// Remapping of roughness to be visually more linear
	if (settings.use_squared_roughness == 1)
	{
		roughness = roughness * roughness;
		coat_roughness = clamp(coat_roughness, 0.089f, 1.0f);
		coat_roughness = coat_roughness * coat_roughness;
	}

	vec3 f0 = mix(vec3(0.04), albedo, metallic);
	vec3 Lo = vec3(0.0);

	// Evaluate direct lighting
	if (settings.use_direct_light == 1)
	{
		for (uint i = 0; i < num_pointlights; ++i)
		{
			PointlightData pointlight = pointlights[i];
			vec3 light_color = pointlight.color * pointlight.intensity;
		
			vec3 L = normalize(pointlight.position - world_pos.xyz);
			vec3 dist_to_light = vec3(length(pointlight.position - world_pos.xyz));
			vec3 dist_attenuation = clamp(1.0 / (falloff.x + (falloff.y * dist_to_light) + (falloff.z * (dist_to_light * dist_to_light))), 0.0, 1.0);

			Lo += BRDFClearCoat(L, light_color, V, N, f0, albedo, metallic, roughness, coat_alpha, coat_normal, coat_roughness) * dist_attenuation;
		}
	}
	
	// Evaluate indirect lighting from HDR environment
	if (settings.use_ibl == 1)
	{
		vec3 R = reflect(-V, N);

		vec2 env_brdf = SampleTexture(push_consts.brdf_lut_index, 0, vec2(max(dot(N, V), 0.0), roughness)).rg;
		vec3 reflection = SampleTextureCubeLod(push_consts.prefiltered_cubemap_index, 0, R, roughness * push_consts.num_prefiltered_mips).rgb;
		vec3 irradiance = SampleTextureCube(push_consts.irradiance_cubemap_index, 0, N).rgb;

		vec3 diffuse = irradiance * albedo * INV_PI;

		vec3 F = F_SchlickRoughness(max(dot(N, V), 0.0), f0, roughness);
		vec3 specular = reflection * (F * env_brdf.x + env_brdf.y);

		// Take into account clearcoat layer for IBL
		if (settings.use_clearcoat_specular_ibl == 1)
		{
			vec3 Fc = F_SchlickRoughness(max(dot(coat_normal, V), 0.0), f0, coat_roughness);
			diffuse *= 1.0 - Fc;
			specular *= pow(1.0 - coat_alpha * Fc, vec3(2.0));

			vec3 Rc = reflect(-V, coat_normal);
			vec3 coat_reflection = SampleTextureCubeLod(push_consts.prefiltered_cubemap_index, 0, Rc, coat_roughness * push_consts.num_prefiltered_mips).rgb;
			vec2 coat_env_brdf = SampleTexture(push_consts.brdf_lut_index, 0, vec2(max(dot(coat_normal, V), 0.0), coat_roughness)).rg;
			specular += coat_alpha * coat_reflection * (Fc * coat_env_brdf.x + coat_env_brdf.y);
		}
	
		vec3 kD = (1.0 - F) * (1.0 - metallic);
		vec3 ambient = (kD * diffuse + specular);
		Lo += ambient;

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
	vec3 albedo = SampleTexture(material.albedo_texture_index, material.sampler_index, frag_tex_coord).rgb * material.albedo_factor.rgb;
	vec3 normal = SampleTexture(material.normal_texture_index, material.sampler_index, frag_tex_coord).rgb;
	vec2 metallic_roughness = SampleTexture(material.metallic_roughness_texture_index, material.sampler_index, frag_tex_coord).bg * vec2(material.metallic_factor, material.roughness_factor);

	// Create the rotation matrix to bring the sampled normal from tangent space to world space
	mat3 TBN = mat3(frag_tangent, frag_bitangent, frag_normal);
	normal = normal * 2.0 - 1.0;
	normal = normalize(TBN * normal);

	vec3 view_pos = camera.view_pos.xyz;
	vec3 view_dir = normalize(view_pos - frag_pos.xyz);

	vec3 color = vec3(0.0);

	if (material.has_clearcoat == 1 && settings.use_clearcoat == 1)
	{
		float clearcoat_alpha = SampleTexture(material.clearcoat_alpha_texture_index, material.sampler_index, frag_tex_coord).r * material.clearcoat_alpha_factor;
		vec3 clearcoat_normal = SampleTexture(material.clearcoat_normal_texture_index, material.sampler_index, frag_tex_coord).rgb;
		float clearcoat_roughness = SampleTexture(material.clearcoat_roughness_texture_index, material.sampler_index, frag_tex_coord).g * material.clearcoat_roughness_factor;

		clearcoat_normal = clearcoat_normal * 2.0 - 1.0;
		clearcoat_normal = normalize(TBN * clearcoat_normal);

		color = RadianceAtFragmentClearCoat(
			view_dir, normal, frag_pos.xyz,
			albedo, metallic_roughness.x, metallic_roughness.y,
			clearcoat_alpha, clearcoat_normal, clearcoat_roughness
		);

		if (settings.debug_render_mode == DEBUG_RENDER_MODE_CLEARCOAT_ALPHA)
		{
			color = vec3(clearcoat_alpha);
		}
		else if (settings.debug_render_mode == DEBUG_RENDER_MODE_CLEARCOAT_NORMAL)
		{
			color = abs(clearcoat_normal);
		}
		else if (settings.debug_render_mode == DEBUG_RENDER_MODE_CLEARCOAT_ROUGHNESS)
		{
			color = vec3(0.0, clearcoat_roughness, 0.0);
		}
	}
	else
	{
		color = RadianceAtFragment(
			view_dir, normal, frag_pos.xyz,
			albedo, metallic_roughness.x, metallic_roughness.y
		);

		// If this is not a clearcoat material and we want a clearcoat debug view,
		// simply write debug pink to indicate that this is not a clearcoat material
		if (settings.debug_render_mode == DEBUG_RENDER_MODE_CLEARCOAT_ALPHA ||
			settings.debug_render_mode == DEBUG_RENDER_MODE_CLEARCOAT_NORMAL ||
			settings.debug_render_mode == DEBUG_RENDER_MODE_CLEARCOAT_ROUGHNESS)
		{
			color = vec3(1.0, 0.0, 1.0);
		}
	}

	switch(settings.debug_render_mode)
	{
		case DEBUG_RENDER_MODE_ALBEDO:
		{
			color = albedo.xyz;
		} break;
		case DEBUG_RENDER_MODE_NORMAL:
		{
			color = abs(normal);
		} break;
		case DEBUG_RENDER_MODE_METALLIC_ROUGHNESS:
		{
			// In source textures, metallic is blue, roughness is green, we mimic that
			color = vec3(0.0, metallic_roughness.yx);
		} break;
	}

	out_color = vec4(color, 1.0);
}
