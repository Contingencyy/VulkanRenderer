#include "Common.glsl"

// Determines what the probability is for finding the normal that would
// reflect the specular from the view direction towards the light direction (PDF)
// The halfway vector (H) is that normal that would reflect towards the light
float D_GGX(float NoH, float a)
{
	float a2 = a * a;
    float f = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / (PI * f * f);
}

// Reflectivity based on incident angle
// Jacco: Do not use for glass!
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

// Self shadowing and masking of the microfacets
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
	return INV_PI;
}

float Fd_Burley(float roughness, float NoL, float NoV, float LoH)
{
	float f90 = 0.5f + 2.0f * roughness * LoH * LoH;
	float light_scatter = F_Schlick90(NoL, 1.0, f90);
	float view_scatter = F_Schlick90(NoV, 1.0, f90);
	return light_scatter * view_scatter * (1.0 * INV_PI);
}

float Fd_OrenNayar(float roughness, float NoL, float NoV, float LoV, vec3 N, vec3 V)
{
	float s = LoV - NoL * NoV;
	
	float sigma2 = roughness * roughness;
	float A = 1.0 - 0.5 * (sigma2 / ((sigma2 + 0.33) + 0.000001));
	float B = 0.45 * sigma2 / ((sigma2 + 0.09) + 0.0001);

	float ga = dot(V - N * NoV, N - N * NoL);
	return max(0.0, NoL) * (A + B * max(0.0, ga) * sqrt(max((1.0 - NoV * NoV) * (1.0 - NoL * NoL), 0.0)) / max(NoL, NoV));
}

vec3 SpecularLobe(float metallic, float roughness, vec3 f0, float NoL, float NoH, float NoV, float LoH, out vec3 kD)
{
	float D = D_GGX(NoH, roughness);
	float G = G_SmithGGXCorrelated(NoV, NoL, roughness);
	vec3 F = F_Schlick(LoH, f0);

	kD = (1.0 - F) * (1.0 - metallic);
	return (D * G) * F;
}

vec3 DiffuseLobe(float roughness, vec3 albedo, float NoL, float NoV, float LoH, float LoV, vec3 N, vec3 V)
{
	switch (settings.pbr_diffuse_brdf_model)
	{
		case DIFFUSE_BRDF_MODEL_LAMBERTIAN:
			return albedo * Fd_Lambert();
		case DIFFUSE_BRDF_MODEL_BURLEY:
			return albedo * Fd_Burley(roughness, NoL, NoV, LoH);
		case DIFFUSE_BRDF_MODEL_OREN_NAYAR:
			return albedo * Fd_OrenNayar(roughness, NoL, NoV, LoV, N, V);
	}
}

/*
	This implementation of clear coat materials is very simple and does not model the following effects:
	- The clearcoat layer is assumed infinitely thin, which means that there is no refraction
	- The ior of the clearcoat and base layer do not influence each other, which is inaccurate
	- There is no scattering between the clear coat and base layer
	- There is no diffraction
	- Clearcoat layer is always assumed to have an IOR of 1.5
*/

vec3 ClearCoatLobe(float roughness, float clearcoat_alpha, vec3 f0, float NoL, float NoH, float NoV, float LoH, out vec3 Fcc)
{
	float D = D_GGX(NoH, roughness);
	float G = G_SmithGGXCorrelated(NoV, NoL, roughness);
	vec3 F = F_Schlick(LoH, f0) * clearcoat_alpha;

	Fcc = F;
	return (D * G) * F;
}
