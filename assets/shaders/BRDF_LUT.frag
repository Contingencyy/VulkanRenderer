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

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

vec2 IntegrateBRDF(float NoV, float roughness)
{
	// In our BRDF implementation we use a squared roughness for perceptually more linear results, so we also square it here
	roughness = roughness * roughness;

	vec3 V = vec3(sqrt(1.0 - NoV * NoV), 0.0, NoV);

	float A = 0.0;
	float B = 0.0;

	vec3 N = vec3(0.0, 0.0, 1.0);

	for (uint i = 0; i < push_consts.num_samples; ++i)
	{
		vec2 Xi = Hammersley2D(i, push_consts.num_samples);
		vec3 H = ImportanceSampleGGX(Xi, N, roughness);
		vec3 L = normalize(2.0 * dot(V, H) * H - V);

		float NoL = max(L.z, 0.0);
		float NoH = max(H.z, 0.0);
		float VoH = max(dot(V, H), 0.0);

		if (NoL > 0.0)
		{
			float G = GeometrySmith(N, V, L, roughness);
			float G_Vis = (G * VoH) / (NoH * NoV);
			float Fc = pow(1.0 - VoH, 5.0);

			A += (1.0 - Fc) * G_Vis;
			B += Fc * G_Vis;
		}
	}

	A /= float(push_consts.num_samples);
	B /= float(push_consts.num_samples);

	return vec2(A, B);
}

void main()
{
	vec2 integrated_brdf = IntegrateBRDF(tex_coord.x, tex_coord.y);
	out_color = integrated_brdf;
}
