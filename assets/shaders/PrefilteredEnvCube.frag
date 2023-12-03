#version 450

#extension GL_KHR_vulkan_glsl : enable
#extension GL_EXT_nonuniform_qualifier : enable

/*

	This shader generates a prefiltered map used in image-based lighting for specular lighting from an HDR environment map

*/

#include "Common.glsl"

layout(std140, push_constant) uniform constants
{
	layout(offset = 64) uint src_texture_index;
	layout(offset = 68) uint src_sampler_index;
	layout(offset = 72) uint num_samples;
	layout(offset = 76) float roughness;
} push_consts;

layout(location = 0) in vec3 local_position;

layout(location = 0) out vec4 out_color;

void main()
{
	vec3 normal = normalize(local_position);
	vec3 R = normal;
	vec3 V = R;

	// In our BRDF implementation we use a squared roughness for perceptually more linear results, so we also square it here
	float roughness = push_consts.roughness * push_consts.roughness;

	float total_weight = 0.0;
	vec3 prefiltered_color = vec3(0.0);

	for (uint i = 0; i < push_consts.num_samples; ++i)
	{
		vec2 Xi = Hammersley2D(i, push_consts.num_samples);
		vec3 H = ImportanceSampleGGX(Xi, normal, roughness);
		vec3 L = normalize(2.0 * dot(V, H) * H - V);

		float NoL = max(dot(normal, L), 0.0);
		if (NoL > 0.0)
		{
			prefiltered_color += SampleTextureCube(push_consts.src_texture_index, push_consts.src_sampler_index, L).rgb * NoL;
			total_weight += NoL;
		}
	}

	prefiltered_color = prefiltered_color / total_weight;
	out_color = vec4(prefiltered_color, 1.0);
}
