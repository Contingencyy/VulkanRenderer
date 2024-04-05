#version 460

/*

	This shader generates a prefiltered map used in image-based lighting for specular lighting from an HDR environment map

*/

#include "BRDF.glsl"

layout(std140, push_constant) uniform PushConsts
{
	layout(offset = 68) uint src_texture_index;
	layout(offset = 72) uint src_sampler_index;
	layout(offset = 76) uint num_samples;
	layout(offset = 80) float roughness;
} push;

layout(location = 0) in vec3 local_position;

layout(location = 0) out vec4 out_color;

void main()
{
	vec3 normal = normalize(local_position);
	vec3 R = normal;
	vec3 V = R;

	float total_weight = 0.0;
	vec3 prefiltered_color = vec3(0.0);

	for (uint i = 0; i < push.num_samples; ++i)
	{
		vec2 Xi = Hammersley2D(i, push.num_samples);
		vec3 H = ImportanceSampleGGX(Xi, push.roughness, normal);
		vec3 L = normalize(2.0 * dot(V, H) * H - V);

		float NoL = clamp(dot(normal, L), 0.0, 1.0);
		if (NoL > 0.0)
		{
			// Improvement for bright dots in lower mips
			// Source: https://chetanjags.wordpress.com/2015/08/26/image-based-lighting/
			float NoH = clamp(dot(normal, H), 0.0, 1.0);
			float HoV = clamp(dot(H, V), 0.0, 1.0);
			float D = D_GGX(NoH, push.roughness);
			float pdf = (D * NoH / (4.0 * HoV)) + 0.0001;
			
			float omega_s = 1.0 / (float(push.num_samples) * pdf);
			float resolution = float(GetTextureDimensions(push.src_texture_index).x);
			float omega_p = 4.0 * PI / (6.0 * resolution * resolution);

			float mip = push.roughness == 0.0 ? 0.0 : max(0.5 * log2(omega_s / omega_p) + 1.0, 0.0);

			prefiltered_color += SampleTextureCubeLod(push.src_texture_index, push.src_sampler_index, L, mip).rgb * NoL;
			total_weight += NoL;
		}
	}

	prefiltered_color = prefiltered_color / total_weight;
	out_color = vec4(prefiltered_color, 1.0);
}
