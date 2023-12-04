#pragma once
#include "Common.glsl"

float D_GGX(float NoH, float a)
{
	float a2 = a * a;
    float f = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / (PI * f * f);
}

vec3 F_Schlick(float u, vec3 f0)
{
	return f0 + (vec3(1.0f) - f0) * pow(1.0f - u, 5.0f);
}

float F_Schlick90(float u, float f0, float f90)
{
	return f0 + (f90 - f0) * pow(1.0f - u, 5.0f);
}

vec3 F_SchlickRoughness(float u, vec3 f0, float roughness)
{
	return f0 + (max(vec3(1.0 - roughness), f0) - f0) * pow(clamp(1.0 - u, 0.0, 1.0), 5.0);
}

float G_SmithGGXCorrelated(float NoV, float NoL, float a)
{
	float a2 = a * a;
    float GGXL = NoV * sqrt((-NoL * a2 + NoL) * NoL + a2);
    float GGXV = NoL * sqrt((-NoV * a2 + NoV) * NoV + a2);
    return 0.5 / (GGXV + GGXL);
}

float V_Kelemen(float LoH, float roughness)
{
	return roughness / (LoH * LoH);
}

float Fd_Lambert()
{
	return 1.0f * INV_PI;
}

float Fd_Burley(float NoV, float NoL, float LoH, float roughness)
{
	float f90 = 0.5f + 2.0f * roughness * LoH * LoH;
	float light_scatter = F_Schlick90(NoL, 1.0f, f90);
	float view_scatter = F_Schlick90(NoV, 1.0f, f90);
	return light_scatter * view_scatter * (1.0f / PI);
}

void BaseContribution(vec3 L, vec3 V, vec3 N, vec3 f0, vec3 albedo, float metallic, float roughness, inout vec3 brdf_diffuse, inout vec3 brdf_specular)
{
	vec3 H = normalize(V + L);

	float NoV = clamp(dot(N, V), 0.0, 1.0);
	float NoL = clamp(dot(N, L), 0.0, 1.0);
	float LoH = clamp(dot(L, H), 0.0, 1.0);
	float NoH = clamp(dot(N, H), 0.0, 1.0);

	//roughness = max(0.05, roughness);

	if (NoL > 0.0)
	{
		float D = D_GGX(NoH, roughness);
		float G = G_SmithGGXCorrelated(NoV, NoL, roughness);
		vec3 F = F_Schlick(NoV, f0);
		
		vec3 kD = vec3(1.0) - F;
		kD *= 1.0 - metallic;

		brdf_specular = (D * G) * F;
		brdf_specular *= NoL;
		brdf_diffuse = kD * albedo * Fd_Burley(NoV, NoL, LoH, roughness);
		brdf_diffuse *= NoL;
	}
}

vec3 BRDF(vec3 L, vec3 light_color, vec3 V, vec3 N, vec3 f0, vec3 albedo, float metallic, float roughness)
{
	vec3 brdf_diffuse = vec3(0.0);
	vec3 brdf_specular = vec3(0.0);
	BaseContribution(L, V, N, f0, albedo, metallic, roughness, brdf_diffuse, brdf_specular);

	vec3 color = (brdf_diffuse + brdf_specular);
	color *= light_color;
	return color;
}

/*
	This implementation of clear coat materials is very simple and does not model the following effects:
	- The clearcoat layer is assumed infinitely thin, which means that there is no refraction
	- The ior of the clearcoat and base layer do not influence each other, which is inaccurate
	- There is no scattering between the clear coat and base layer
	- There is no diffraction
	- Clearcoat layer is always assumed to have an IOR of 1.5
*/

void ClearCoatContribution(vec3 L, vec3 V, vec3 N, vec3 f0, float roughness, inout vec3 Fc, inout vec3 brdf_clearcoat)
{
	vec3 H = normalize(V + L);

	float NoV = clamp(dot(N, V), 0.0, 1.0);
	float NoL = clamp(dot(N, L), 0.0, 1.0);
	float LoH = clamp(dot(L, H), 0.0, 1.0);
	float NoH = clamp(dot(N, H), 0.0, 1.0);

	if (NoL > 0.0)
	{
		float D = D_GGX(NoH, roughness);
		float G = G_SmithGGXCorrelated(NoV, NoL, roughness);
		vec3 F = F_Schlick(LoH, f0);

		brdf_clearcoat = (D * G) * F;
		brdf_clearcoat *= NoL;
	}
}

vec3 BRDFClearCoat(vec3 L, vec3 light_color, vec3 V, vec3 N, vec3 f0, vec3 albedo, float metallic, float roughness,
	float coat_alpha, vec3 coat_normal, float coat_roughness)
{
	vec3 Fc = vec3(0.0);
	vec3 brdf_clearcoat = vec3(0.0);
	ClearCoatContribution(L, V, coat_normal, f0, coat_roughness, Fc, brdf_clearcoat);

	vec3 brdf_diffuse = vec3(0.0);
	vec3 brdf_specular = vec3(0.0);
	BaseContribution(L, V, N, f0, albedo, metallic, roughness, brdf_diffuse, brdf_specular);

	vec3 color = (brdf_diffuse + brdf_specular) * (1.0 - coat_alpha * Fc) + coat_alpha * brdf_clearcoat;
	color *= light_color;
	return color;
}
