#version 450

#extension GL_KHR_vulkan_glsl : enable
#extension GL_EXT_nonuniform_qualifier : enable

/*

	This shader generates the BRDF look-up table (BRDF integration map) used in image-based lighting for the specular light

*/

#include "BRDF.glsl"

layout(std140, push_constant) uniform constants
{
	layout(offset = 0) uint num_samples;
} push_consts;

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

float G_SchlicksmithGGX(float dotNL, float dotNV, float roughness)
{
	float k = (roughness * roughness) / 2.0;
	float GL = dotNL / (dotNL * (1.0 - k) + k);
	float GV = dotNV / (dotNV * (1.0 - k) + k);
	return GL * GV;
}

vec2 IntegrateBRDF(float NoV, float roughness)
{
	// In our BRDF implementation we use a squared roughness for perceptually more linear results, so we also square it here
	roughness = roughness * roughness;

	const vec3 N = vec3(0.0, 0.0, 1.0);
	vec3 V = vec3(sqrt(1.0 - NoV * NoV), 0.0, NoV);

	vec2 LUT = vec2(0.0);
	for(uint i = 0u; i < push_consts.num_samples; i++)
	{
		vec2 Xi = Hammersley2D(i, push_consts.num_samples);
		vec3 H = ImportanceSampleGGX(Xi, N, roughness);
		vec3 L = 2.0 * dot(V, H) * H - V;

		float NoL = max(dot(N, L), 0.0);
		float NoV = max(dot(N, V), 0.0);
		float VoH = max(dot(V, H), 0.0); 
		float NoH = max(dot(H, N), 0.0);

		if (NoL > 0.0)
		{
			float G = G_SchlicksmithGGX(NoL, NoV, roughness);
			float G_Vis = (G * VoH) / (NoH * NoV);
			float Fc = pow(1.0 - VoH, 5.0);

			LUT += vec2((1.0 - Fc) * G_Vis, Fc * G_Vis);
		}
	}

	return LUT / float(push_consts.num_samples);
}

void main()
{
	vec2 integrated_brdf = IntegrateBRDF(tex_coord.x, tex_coord.y);
	out_color = integrated_brdf;
}
