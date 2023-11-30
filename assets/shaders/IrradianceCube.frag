#version 450

#extension GL_KHR_vulkan_glsl : enable
#extension GL_EXT_nonuniform_qualifier : enable

/*

	This shader generates the irradiance map for a given HDR environment map
	Irradiance maps are used for diffuse IBL, we need to capture the irradiance coming
	from all possible directions over the hemisphere for any given direction of the cube map (convolution)

*/

#include "Common.glsl"

layout(std140, push_constant) uniform constants
{
	layout(offset = 64) uint hdr_tex_idx;
	layout(offset = 68) uint hdr_samp_idx;
	layout(offset = 72) float delta_phi;
	layout(offset = 76) float delta_theta;
} push_constants;

layout(location = 0) in vec3 local_position;

layout(location = 0) out vec4 out_color;

const vec2 inv_atan = vec2(0.1591, 0.3183);
vec2 SampleSpherical(vec3 dir)
{
	vec2 uv = vec2(atan(dir.z, dir.x), asin(dir.y));
	uv *= inv_atan;
	uv += 0.5;

	return uv;
}

void main()
{
	// Using the local position as a direction for the cube map sample
	vec3 N = normalize(local_position);
	vec3 up = vec3(0.0, 1.0, 0.0);
	vec3 right = normalize(cross(up, N));
	up = cross(N, right);

	vec3 irradiance = vec3(0.0);
	uint num_samples = 0;

	// Take a set amount of samples for irradiance cube map
	// Azimuth, two pi
	for (float phi = 0.0; phi < TWO_PI; phi += push_constants.delta_phi)
	{
		// Zenith, half pi
		for (float theta = 0.0; theta < HALF_PI; theta += push_constants.delta_theta)
		{
			vec3 temp = cos(phi) * right + sin(phi) * up;
			vec3 sample_dir = cos(theta) * N + sin(theta) * temp;
			vec2 uv = vec2(atan(sample_dir.x, sample_dir.z) / TWO_PI + 0.5, sample_dir.y * 0.5 + 0.5);
			// Weighting the final result by sin(theta) to compensate for the hemisphere having smaller sample areas towards the top
			irradiance += SampleTexture(push_constants.hdr_tex_idx, push_constants.hdr_samp_idx, uv).rgb * cos(theta) * sin(theta);
			
			num_samples++;
		}
	}
	
	out_color = vec4(PI * irradiance / float(num_samples), 1.0);
}
