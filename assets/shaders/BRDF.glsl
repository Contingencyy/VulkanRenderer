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
	return INV_PI;
}

float Fd_Burley(vec3 L, vec3 V, vec3 N, float roughness)
{
	vec3 H = normalize(V + L);

	float NoV = clamp(dot(N, V), 0.0, 1.0);
	float NoL = clamp(dot(N, L), 0.0, 1.0);
	float LoH = clamp(dot(L, H), 0.0, 1.0);

	float f90 = 0.5f + 2.0f * roughness * LoH * LoH;
	float light_scatter = F_Schlick90(NoL, 1.0, f90);
	float view_scatter = F_Schlick90(NoV, 1.0, f90);
	return light_scatter * view_scatter * (1.0 * INV_PI);
}

float Fd_OrenNayar(vec3 L, vec3 V, vec3 N, float roughness)
{
	float NoV = clamp(dot(N, V), 0.0, 1.0);
	float NoL = clamp(dot(N, L), 0.0, 1.0);
	float LoV = clamp(dot(L, V), 0.0, 1.0);

	float s = LoV - NoL * NoV;
	
	float sigma2 = roughness * roughness;
	float A = 1.0 - 0.5 * (sigma2 / ((sigma2 + 0.33) + 0.000001));
	float B = 0.45 * sigma2 / ((sigma2 + 0.09) + 0.0001);

	float ga = dot(V - N * NoV, N - N * NoL);
	return max(0.0, NoL) * (A + B * max(0.0, ga) * sqrt(max((1.0 - NoV * NoV) * (1.0 - NoL * NoL), 0.0)) / max(NoL, NoV));
}

void EvaluateBRDFBase(vec3 L, vec3 V, vec3 N, vec3 f0, vec3 albedo, float metallic, float roughness, inout vec3 brdf_diffuse, inout vec3 brdf_specular)
{
	vec3 H = normalize(V + L);

	float NoV = clamp(dot(N, V), 0.0, 1.0);
	float NoL = clamp(dot(N, L), 0.0, 1.0);
	float LoH = clamp(dot(L, H), 0.0, 1.0);
	float NoH = clamp(dot(N, H), 0.0, 1.0);
	float LoV = clamp(dot(L, V), 0.0, 1.0);

	//roughness = max(0.05, roughness);

	if (NoL > 0.0)
	{
		float D = D_GGX(NoH, roughness);
		float G = G_SmithGGXCorrelated(NoV, NoL, roughness);
		vec3 F = F_Schlick(NoV, f0);
		
		vec3 kD = (1.0 - F) * (1.0 - metallic);

		brdf_specular = (D * G) * F;
		
		switch (settings.pbr_diffuse_brdf_model)
		{
			case DIFFUSE_BRDF_MODEL_LAMBERTIAN:
				brdf_diffuse = kD * albedo * Fd_Lambert();
				break;
			case DIFFUSE_BRDF_MODEL_BURLEY:
				brdf_diffuse = kD * albedo * Fd_Burley(L, V, N, roughness);
				break;
			case DIFFUSE_BRDF_MODEL_OREN_NAYAR:
				brdf_diffuse = kD * albedo * Fd_OrenNayar(L, V, N, roughness);
				break;
		}
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

void EvaluateBRDFClearCoat(vec3 L, vec3 V, vec3 N, vec3 f0, float roughness, inout vec3 Fc, inout vec3 brdf_clearcoat)
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
	}
}
