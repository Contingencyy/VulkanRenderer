#version 460

/*

	This shader generates the BRDF look-up table (BRDF integration map) used in image-based lighting for the specular light

*/

#include "BRDF.glsl"

layout(std140, push_constant) uniform PushConsts
{
	layout(offset = 0) uint num_samples;
} push;

layout(location = 0) in vec2 tex_coord;

layout(location = 0) out vec2 out_color;

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float a = roughness;
    float k = (a * a) / 2.0;

    float nom   = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}

float G_SchlicksmithGGX(float NoL, float NoV, float roughness)
{
	float k = (roughness * roughness) / 2.0;
	float GL = NoL / (NoL * (1.0 - k) + k);
	float GV = NoV / (NoV * (1.0 - k) + k);
	return GL * GV;
}

vec2 IntegrateBRDF(float NoV, float roughness)
{
	const vec3 N = vec3(0.0, 0.0, 1.0);
	vec3 V = vec3(sqrt(1.0 - NoV * NoV), 0.0, NoV);

	vec2 LUT = vec2(0.0);
	for(uint i = 0u; i < push.num_samples; ++i)
	{
		vec2 Xi = Hammersley2D(i, push.num_samples);
		vec3 H = ImportanceSampleGGX(Xi, roughness, N);
		vec3 L = 2.0 * dot(V, H) * H - V;

		float dotNL = max(dot(N, L), 0.0);
		float dotNV = max(dot(N, V), 0.0);
		float dotVH = max(dot(V, H), 0.0); 
		float dotNH = max(dot(H, N), 0.0);

		if (dotNL > 0.0)
		{
			float G = G_SchlicksmithGGX(dotNL, dotNV, roughness);
			float G_Vis = (G * dotVH) / (dotNH * dotNV);
			float Fc = pow(1.0 - dotVH, 5.0);

			//LUT += vec2(G_Vis * Fc, G_Vis);
			LUT += vec2((1.0 - Fc) * G_Vis, Fc * G_Vis);
		}
	}

	return LUT / float(push.num_samples);
}

void main()
{
	vec2 integrated_brdf = IntegrateBRDF(tex_coord.s, tex_coord.t);
	out_color = integrated_brdf;
}
