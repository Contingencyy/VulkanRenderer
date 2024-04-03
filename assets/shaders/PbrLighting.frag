#version 460

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
	layout(offset = 28) uint tlas_index;
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

const float LUT_SIZE = 64;
const float LUT_SCALE = (LUT_SIZE - 1.0) / LUT_SIZE;
const float LUT_BIAS = 0.5 / LUT_SIZE;

struct ViewInfo
{
	vec3 pos;
	vec3 dir;
};

struct PixelInfo
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

	vec3 f0;
};

bool TraceShadowRay(vec3 ray_origin, vec3 ray_dir, float tmax)
{
	const float tmin = 0.01f;
	bool occluded = false;

	rayQueryEXT ray_query;
	rayQueryInitializeEXT(ray_query, tlas_scene[push_consts.tlas_index], gl_RayFlagsTerminateOnFirstHitEXT, 0xFF, ray_origin, tmin, ray_dir, tmax);
	rayQueryProceedEXT(ray_query);

	// Starts the traversal, once the traversal is complete, rayQueryProceedEXT will return false
	// TODO: Add transparency check, return an occlusion factor (1 - alpha) instead of a bool
//	while (rayQueryProceedEXT(ray_query))
//	{
//	}

	if (rayQueryGetIntersectionTypeEXT(ray_query, true) != gl_RayQueryCommittedIntersectionNoneEXT)
	{
		occluded = true;
	}

	return occluded;
}

vec3 IntegrateEdgeVec(vec3 v1, vec3 v2)
{
    float x = dot(v1, v2);
    float y = abs(x);

    float a = 0.8543985 + (0.4965155 + 0.0145206 * y) * y;
    float b = 3.4175940 + (4.1616724 + y) * y;
    float v = a / b;

    float theta_sintheta = (x > 0.0) ? v : 0.5 * inversesqrt(max(1.0 - x * x, 1e-7)) - v;

    return cross(v1, v2) * theta_sintheta;
}

// Uses Linearly Transformed Cosines approach
vec3 AreaLightIrradiance(ViewInfo view, PixelInfo pixel, mat3 Minv, vec3 area_light_verts[4], bool two_sided)
{
	// Orthonormal basis around surface normal
	vec3 T1, T2;
    T1 = normalize(view.dir - pixel.normal * dot(view.dir, pixel.normal));
    T2 = cross(pixel.normal, T1);

	// Rotate area light in (T1, T2, N) basis
    Minv = Minv * transpose(mat3(T1, T2, pixel.normal));

	vec3 L[4];
	L[0] = Minv * (area_light_verts[0] - pixel.pos_world);
	L[1] = Minv * (area_light_verts[1] - pixel.pos_world);
	L[2] = Minv * (area_light_verts[2] - pixel.pos_world);
	L[3] = Minv * (area_light_verts[3] - pixel.pos_world);

	// Check if the surface point is behind the light
	vec3 dir = area_light_verts[0] - pixel.pos_world;
	vec3 light_normal = cross(area_light_verts[1] - area_light_verts[0], area_light_verts[3] - area_light_verts[0]);
	bool back_face = (dot(dir, light_normal) < 0.0f);

	L[0] = normalize(L[0]);
	L[1] = normalize(L[1]);
	L[2] = normalize(L[2]);
	L[3] = normalize(L[3]);

	vec3 vsum = vec3(0.0);
	vsum += IntegrateEdgeVec(L[0], L[1]);
	vsum += IntegrateEdgeVec(L[1], L[2]);
	vsum += IntegrateEdgeVec(L[2], L[3]);
	vsum += IntegrateEdgeVec(L[3], L[0]);

	// Form factor of the area light polygon
	float len = length(vsum);

	float z = vsum.z / len;
	if (back_face)
	{
		z = -z;
	}

	vec2 uv_form_factor = vec2(z * 0.5f + 0.5f, len);
	uv_form_factor = uv_form_factor * LUT_SCALE + LUT_BIAS;

	// Fetch form factor for the horizon clipping
	float scale = SampleTexture(ltc2_index, materials[push_consts.mat_index].sampler_index, uv_form_factor).w;

	float sum = len * scale;
	if (!back_face && !two_sided)
	{
		sum = 0.0f;
	}

	// Outgoing radiance of the area light towards surface
	vec3 Lo_i = vec3(sum, sum, sum);
	return Lo_i;
}

vec3 ShadePixel(ViewInfo view, PixelInfo pixel)
{
	vec3 Lo = vec3(0.0);

	// Direct lighting
	if (settings.use_direct_light == 1)
	{
//		for (uint i = 0; i < num_pointlights; ++i)
//		{
//			PointlightData pointlight = pointlights[i];
//			vec3 light_color = pointlight.color * pointlight.intensity;
//
//			vec3 light_dir = normalize(pointlight.pos - pixel.pos_world);
//			vec3 dist_to_light = vec3(length(pointlight.pos - pixel.pos_world));
//			vec3 dist_attenuation = clamp(1.0 / (falloff.x + (falloff.y * dist_to_light) + (falloff.z * (dist_to_light * dist_to_light))), 0.0, 1.0);
//			
//			float NoL = clamp(dot(pixel.normal, light_dir), 0.0, 1.0);
//
//			if (NoL > 0.0)
//			{
//				bool occluded = TraceShadowRay(pixel.pos_world + light_dir * 0.01f, light_dir, dist_to_light.x);
//
//				if (!occluded)
//				{
//					vec3 H = normalize(view.dir + light_dir);
//			
//					float NoH = clamp(dot(pixel.normal, H), 0.0, 1.0);
//					float NoV = abs(dot(pixel.normal, view.dir)) + 1e-5;
//					float LoH = clamp(dot(light_dir, H), 0.0, 1.0);
//					float LoV = clamp(dot(light_dir, view.dir), 0.0, 1.0);
//
//					vec3 kD = vec3(0.0);
//					vec3 Fr = SpecularLobe(pixel.metallic, pixel.roughness, pixel.f0, NoL, NoH, NoV, LoH, kD);
//					vec3 Fd = DiffuseLobe(pixel.roughness, pixel.albedo, NoL, NoV, LoH, LoV, pixel.normal, view.dir);
//
//					vec3 color = (kD * Fd) + Fr;
//
//					if (pixel.has_coat)
//					{
//						float NoLc = clamp(dot(pixel.normal_coat, light_dir), 0.0, 1.0);
//						float NoHc = clamp(dot(pixel.normal_coat, H), 0.0, 1.0);
//						float NoVc = abs(dot(pixel.normal_coat, view.dir)) + 1e-5;
//
//						vec3 Fcc = vec3(0.0);
//						vec3 Frc = ClearCoatLobe(pixel.roughness_coat, pixel.alpha_coat, pixel.f0, NoLc, NoHc, NoVc, LoH, Fcc);
//						vec3 attenuation = 1.0 - Fcc;
//
//						color *= attenuation * NoL;
//						color += Frc * NoLc;
//
//						Lo += color * light_color * dist_attenuation;
//					}
//					else
//					{
//						Lo += color * light_color * NoL * dist_attenuation;
//					}
//				}
//			}
//		}

		for (uint i = 0; i < num_area_lights; ++i)
		{
			GPUAreaLight area_light = area_lights[i];

			float NoV = clamp(dot(pixel.normal, view.dir), 0.0f, 1.0f);

			// Fetch matrix
			vec2 uv = vec2(pixel.roughness, sqrt(1.0 - NoV));
			uv = uv * LUT_SCALE + LUT_BIAS;

			vec4 ltc1 = SampleTexture(ltc1_index, materials[push_consts.mat_index].sampler_index, uv);
			vec4 ltc2 = SampleTexture(ltc2_index, materials[push_consts.mat_index].sampler_index, uv);

			vec3 area_light_color = SampleTexture(area_light.texture_index, materials[push_consts.mat_index].sampler_index, uv).rgb;
			area_light_color = area_light_color * vec3(area_light.color_red, area_light.color_green, area_light.color_blue);
			//vec4 area_light_texture = SampleTextureLod(area_light.texture_index, materials[push_consts.mat_index].sampler_index, uv, 0.0f);

			mat3 Minv = mat3(
				vec3(ltc1.x, 0.0f, ltc1.y),
				vec3(0.0f,   1.0f, 0.0f),
				vec3(ltc1.z, 0.0f, ltc1.w)
			);

			//vec3 area_light_color = vec3(area_light.color_red, area_light.color_green, area_light.color_blue);
			vec3 area_light_verts[4] = { area_light.vert0, area_light.vert1, area_light.vert2, area_light.vert3 };

			vec3 diffuse = AreaLightIrradiance(view, pixel, mat3(1), area_light_verts, area_light.two_sided);
			vec3 specular = AreaLightIrradiance(view, pixel, Minv, area_light_verts, area_light.two_sided);
			specular *= pixel.f0 * ltc2.x + (1.0f - pixel.f0) * ltc2.y;

			Lo += area_light_color * area_light.intensity * (specular + pixel.albedo * diffuse);
		}
	}

	// Indirect lighting from HDR environment
	if (settings.use_ibl == 1)
	{
		vec3 R = reflect(-view.dir, pixel.normal);

		vec2 env_brdf = SampleTexture(push_consts.brdf_lut_index, push_consts.brdf_lut_sampler_index, vec2(max(dot(pixel.normal, view.dir), 0.0), pixel.roughness)).rg;
		vec3 reflection = SampleTextureCubeLod(push_consts.prefiltered_cubemap_index, push_consts.prefiltered_sampler_index, R, pixel.roughness * push_consts.num_prefiltered_mips).rgb;
		vec3 irradiance = SampleTextureCube(push_consts.irradiance_cubemap_index, push_consts.irradiance_sampler_index, pixel.normal).rgb;

		vec3 diffuse_color = pixel.albedo * Fd_Lambert();
		vec3 diffuse = irradiance * diffuse_color;

		vec3 F = F_SchlickRoughness(max(dot(pixel.normal, view.dir), 0.0), pixel.f0, pixel.roughness);
		// Fss: Single scatter BRDF - Ess: Single scattering directional albedo
		vec3 FssEss = F * env_brdf.x + env_brdf.y;

		// Take into account clearcoat layer for IBL
		if (settings.use_ibl_clearcoat == 1 && pixel.has_coat)
		{
			vec3 Fcc = F_SchlickRoughness(max(dot(pixel.normal_coat, view.dir), 0.0), pixel.f0, pixel.roughness_coat);
			vec3 attenuation = 1.0 - pixel.alpha_coat * Fcc;

			vec3 Rc = reflect(-view.dir, pixel.normal_coat);
			vec3 reflection_coat = SampleTextureCubeLod(push_consts.prefiltered_cubemap_index, push_consts.prefiltered_sampler_index, Rc, pixel.roughness_coat * push_consts.num_prefiltered_mips).rgb;
			vec2 env_brdf_coat = SampleTexture(push_consts.brdf_lut_index, push_consts.brdf_lut_sampler_index, vec2(max(dot(pixel.normal_coat, view.dir), 0.0), pixel.roughness_coat)).rg;
			
			// Take into account energy lost in the clearcoat layer by adjusting the base layer
			diffuse *= attenuation;

			FssEss *= attenuation;
			FssEss += pixel.alpha_coat * (Fcc * env_brdf_coat.x + env_brdf_coat.y);

			reflection *= attenuation;
			reflection += pixel.alpha_coat * reflection_coat;

			env_brdf *= 1.0 - pixel.alpha_coat;
			env_brdf += pixel.alpha_coat * env_brdf_coat;
		}

		vec3 kD = vec3(0.0);
		// Source (Fdez-Ag�era): https://www.jcgt.org/published/0008/01/03/paper.pdf
		// This approach simulates the energy lost due do single scattering by adding a second specular lobe for the multiscatter.
		// Fss + Fms = 1 - This should hold true to achieve the energy conservation we are after
		// After the first bounce, we treat light as if it will be scattered randomly in all directions.
		// Therefore we can treat these secondary bounces as uniform energy in all directions
		// so we represent this as an attenuated form of the cosine-weighted irradiance
		if (settings.use_ibl_multiscatter == 1)
		{
			// Energy lost due to single scattering
			float Ems = (1.0 - (env_brdf.x + env_brdf.y));

			// On each bounce we lose a fraction of energy due to it escaping the surface or being absorbed
			// This means that only F_avg energy can participate in the next bounce
			// F_Avg: Cosine-weighted average of the fresnel term
			vec3 F_avg = pixel.f0 + (1.0 - pixel.f0) / 21.0;

			// F_avg was not squared in the original paper, however it was later adjusted because the model includes
			// single scattering events, which are already accounted for in Fss, and we do not want that here
			// https://blog.selfshadow.com/2018/06/04/multi-faceted-part-2/
			// https://blog.selfshadow.com/publications/s2017-shading-course/imageworks/s2017_pbs_imageworks_slides_v2.pdf
			vec3 FmsEms = Ems * FssEss * pow(F_avg, vec3(2.0)) / (1.0 - F_avg * Ems);

			// FmsEms + kD: Fixes energy conservation for dielectrics/non perfect mirrors
			kD = diffuse_color * (1.0 - FssEss - FmsEms);
			Lo += (FmsEms + kD) * irradiance + FssEss * reflection;
		}
		else
		{
			kD = (1.0 - F) * (1.0 - pixel.metallic);
			Lo += kD * diffuse + FssEss * reflection;
		}

		if (settings.debug_render_mode == DEBUG_RENDER_MODE_IBL_INDIRECT_DIFFUSE)
		{
			Lo = kD * diffuse;
		}
		else if (settings.debug_render_mode == DEBUG_RENDER_MODE_IBL_INDIRECT_SPECULAR)
		{
			Lo = FssEss * reflection;
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
	// View info for lighting
	ViewInfo view;
	view.pos = camera.view_pos.xyz;
	view.dir = normalize(view.pos - frag_pos.xyz);

	GPUMaterial material = materials[push_consts.mat_index];

	PixelInfo pixel;
	pixel.has_coat = false;
	pixel.alpha_coat = 0.0;
	pixel.normal_coat = vec3(0.0);
	pixel.roughness_coat = 0.0;

	// Sample default material values
	pixel.pos_world = frag_pos.xyz;
	pixel.albedo = SampleTexture(material.albedo_texture_index, material.sampler_index, frag_tex_coord).rgb * material.albedo_factor.rgb;
	vec3 sampled_normal = SampleTexture(material.normal_texture_index, material.sampler_index, frag_tex_coord).rgb;
	vec2 sampled_metallic_roughness = SampleTexture(material.metallic_roughness_texture_index, material.sampler_index, frag_tex_coord).bg * vec2(material.metallic_factor, material.roughness_factor);
	pixel.metallic = sampled_metallic_roughness.x;
	pixel.roughness = sampled_metallic_roughness.y;

	// Bring sampled normal from tangent to world space
	mat3 TBN = mat3(frag_tangent, frag_bitangent, frag_normal);
	sampled_normal = sampled_normal * 2.0 - 1.0;
	pixel.normal = normalize(TBN * sampled_normal);

	if (settings.use_pbr_clearcoat == 1 && material.has_clearcoat == 1)
	{
		pixel.has_coat = true;

		// Sample clearcoat material values
		pixel.alpha_coat = SampleTexture(material.clearcoat_alpha_texture_index, material.sampler_index, frag_tex_coord).r * material.clearcoat_alpha_factor;
		vec3 sampled_normal_coat = SampleTexture(material.clearcoat_normal_texture_index, material.sampler_index, frag_tex_coord).rgb;
		pixel.roughness_coat = SampleTexture(material.clearcoat_roughness_texture_index, material.sampler_index, frag_tex_coord).g * material.clearcoat_roughness_factor;
		
		// Bring sampled clearcoat normal from tangent to world space
		sampled_normal_coat = sampled_normal_coat * 2.0 - 1.0;
		pixel.normal_coat = normalize(TBN * sampled_normal_coat);
	}

	// Square the roughness to be perceptually more linear, if the setting is enabled
	// https://google.github.io/filament/Filament.md.html#materialsystem/parameterization/remapping 4.8.3.3
	if (settings.use_pbr_squared_roughness == 1)
	{
		pixel.roughness = clamp(pixel.roughness, 0.089, 1.0);
		pixel.roughness = pow(pixel.roughness, 2.0);
		pixel.roughness_coat = clamp(pixel.roughness_coat, 0.089, 1.0);
		pixel.roughness_coat = pow(pixel.roughness_coat, 2.0);
	}

	// Evaluate f0 and energy compensation for direct lighting
	vec2 env_brdf = SampleTexture(push_consts.brdf_lut_index, push_consts.brdf_lut_sampler_index, vec2(max(dot(pixel.normal, view.dir), 0.0), pixel.roughness)).rg;
	pixel.f0 = mix(vec3(0.04), pixel.albedo, pixel.metallic);

	// Write final color
	vec3 color = vec3(0.0f);
	if (material.blackbody_radiator == 1)
	{
		color = material.albedo_factor.xyz * pixel.albedo;
	}
	else
	{
		color = ShadePixel(view, pixel);
	}

	// Debug render modes
	switch (settings.debug_render_mode)
	{
		case DEBUG_RENDER_MODE_ALBEDO:
		{
			color = pixel.albedo;
			break;
		}
		case DEBUG_RENDER_MODE_NORMAL:
		{
			color = abs(pixel.normal);
			break;
		}
		case DEBUG_RENDER_MODE_METALLIC_ROUGHNESS:
		{
			color = vec3(0.0, pixel.roughness, pixel.metallic);
			break;
		}
		case DEBUG_RENDER_MODE_CLEARCOAT_ALPHA:
		{
			if (material.has_clearcoat == 1)
				color = vec3(pixel.alpha_coat);
			else
				color = vec3(1.0, 0.0, 1.0);
			break;
		}
		case DEBUG_RENDER_MODE_CLEARCOAT_NORMAL:
		{
			if (material.has_clearcoat == 1)
				color = pixel.normal_coat;
			else
				color = vec3(1.0, 0.0, 1.0);
			break;
		}
		case DEBUG_RENDER_MODE_CLEARCOAT_ROUGHNESS:
		{
			if (material.has_clearcoat == 1)
				color = vec3(0.0, pixel.roughness_coat, 0.0);
			else
				color = vec3(1.0, 0.0, 1.0);
			break;
		}
	}

	out_color = vec4(color, 1.0);
}
