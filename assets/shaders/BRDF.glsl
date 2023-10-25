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

vec3 BRDFSpecular(vec3 f0, float roughness, float NoH, float LoH, float NoV, float NoL)
{
	float D = D_GGX(NoH, roughness);
	vec3 F = F_Schlick(LoH, f0);
	float V = V_SmithGGXCorrelated(NoV, NoL, roughness);

	return (D * V) * F;
}

void EvaluateBRDF(vec3 view_dir, vec3 light_dir, vec3 H, float NoL, float LoH, vec3 normal, vec3 base_color, float metallic, float roughness, out vec3 brdf_specular, out vec3 brdf_diffuse)
{
	vec3 f0 = vec3(0.04f);
	f0 = mix(f0, base_color, metallic);

	float NoV = abs(dot(normal, view_dir)) + 1e-5f;
	float NoH = clamp(dot(normal, H), 0.0f, 1.0f);

	// Remapping of roughness to be visually more linear
	roughness = roughness * roughness;

	float D = D_GGX(NoH, roughness);
	vec3 F = F_Schlick(LoH, f0);
	float V = V_SmithGGXCorrelated(NoV, NoL, roughness);

	brdf_specular = BRDFSpecular(f0, roughness, NoH, LoH, NoV, NoL);
	brdf_diffuse = (vec3(1.0f) - F) * base_color * Fd_Burley(NoV, NoL, LoH, roughness);
}

float V_Kelemen(float LoH, float roughness)
{
	return roughness / (LoH * LoH);
}

void EvaluateBRDFClearCoat(vec3 view_dir, vec3 light_dir, vec3 H, float LoH, vec3 clearcoat_normal, float clearcoat_roughness, out vec3 Fc, out vec3 brdf_clearcoat)
{
	float NoV = abs(dot(clearcoat_normal, view_dir)) + 1e-5f;
	float NoL = clamp(dot(clearcoat_normal, light_dir), 0.0f, 1.0f);
	float NoH = clamp(dot(clearcoat_normal, H), 0.0f, 1.0f);

	// Remapping of roughness to be visually more linear
	clearcoat_roughness = clamp(clearcoat_roughness, 0.089f, 1.0f);
	clearcoat_roughness = clearcoat_roughness * clearcoat_roughness;
	
	// TODO: FIX
	vec3 f0 = vec3(0.04f);
	Fc = F_Schlick(LoH, f0);
	brdf_clearcoat = BRDFSpecular(f0, clearcoat_roughness, NoH, LoH, NoV, NoL);
}
