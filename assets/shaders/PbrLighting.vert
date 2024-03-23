#version 450

#extension GL_EXT_nonuniform_qualifier : enable

#include "Common.glsl"

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 tex_coord;
layout(location = 2) in vec3 normal;
layout(location = 3) in vec4 tangent;
// Takes up 4 locations, 5 to 8
layout(location = 4) in mat4 transform;

layout(location = 0) out vec4 frag_pos;
layout(location = 1) out vec2 frag_tex_coord;
layout(location = 2) out vec3 frag_normal;
layout(location = 3) out vec3 frag_tangent;
layout(location = 4) out vec3 frag_bitangent;

void main()
{
	frag_pos = transform * vec4(position, 1.0f);
	frag_tex_coord = tex_coord;

	frag_normal = normalize(transform * vec4(normal, 0.0f)).xyz;
	frag_tangent = normalize(transform * vec4(tangent.xyz, 0.0f)).xyz;
	// NOTE: Larger meshes sometimes average tangents for smoothness, so we need to use this trick
	// to re-orthogonalize the tangent (Gram-Schmidt process)
	frag_tangent = normalize(frag_tangent - dot(frag_tangent, frag_normal) * frag_normal);
	frag_bitangent = normalize(cross(frag_normal, frag_tangent)) * (-tangent.w);
	
	gl_Position = camera.proj * camera.view * frag_pos;
}
