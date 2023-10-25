#pragma once
#define PI 3.14159265f

float D_GGX(float NoH, float a)
{
	float a2 = a * a;
	float f = (NoH * a2 - NoH) * NoH + 1.0f;
	return a2 / (PI * f * f);
}

vec3 F_Schlick(float u, vec3 f0)
{
	return f0 + (vec3(1.0f) - f0) * pow(1.0f - u, 5.0f);
}

float V_SmithGGXCorrelated(float NoV, float NoL, float a)
{
	float a2 = a * a;
	float GGXL = NoV * sqrt((-NoL * a2 + NoL) * NoL + a2);
	float GGXV = NoL * sqrt((-NoV * a2 + NoV) * NoV + a2);
	return 0.5f / (GGXV + GGXL);
}

float Fd_Lambert()
{
	return 1.0f / PI;
}

float F_Schlick90(float u, float f0, float f90)
{
	return f0 + (f90 - f0) * pow(1.0f - u, 5.0f);
}

float Fd_Burley(float NoV, float NoL, float LoH, float roughness)
{
	float f90 = 0.5f + 2.0f * roughness * LoH * LoH;
	float light_scatter = F_Schlick90(NoL, 1.0f, f90);
	float view_scatter = F_Schlick90(NoV, 1.0f, f90);
	return light_scatter * view_scatter * (1.0f / PI);
}

void EvaluateBRDF(vec3 H, float NoV, float NoL, float NoH, float LoH, vec3 normal, vec3 base_color, float metallic, float roughness, out vec3 brdf_specular, out vec3 brdf_diffuse)
{
	vec3 f0 = vec3(0.04f);
	f0 = mix(f0, base_color, metallic);

	// Remapping of roughness to be visually more linear
	roughness = roughness * roughness;

	float D = D_GGX(NoH, roughness);
	vec3 F = F_Schlick(LoH, f0);
	float V = V_SmithGGXCorrelated(NoV, NoL, roughness);

	brdf_specular = (D * V) * F;
	brdf_diffuse = (vec3(1.0f) - F) * base_color * Fd_Burley(NoV, NoL, LoH, roughness);
}

float V_Kelemen(float LoH, float roughness)
{
	return roughness / (LoH * LoH);
}

void EvaluateBRDFClearCoat(float alpha, float roughness, float NoH, float LoH, inout vec3 brdf_specular)
{
	// Remapping of roughness to be visually more linear
	roughness = clamp(roughness, 0.089f, 1.0f);
	roughness = roughness * roughness;
	
	float D = D_GGX(NoH, roughness);
	float V = V_Kelemen(LoH, roughness);
	vec3 F = F_Schlick(LoH, vec3(0.04f)) * alpha;

	vec3 clearcoat_specular = (D * V) * F;
	brdf_specular = (brdf_specular * (1.0f - F)) * (1.0f - F) + clearcoat_specular;
}
