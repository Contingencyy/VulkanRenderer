#version 450
#include "Common.glsl"

layout(std140, push_constant) uniform PushConsts
{
	layout(offset = 0) uint ib_index;
	layout(offset = 4) uint vb_index;
} push;

layout(location = 0) out vec4 frag_pos;
layout(location = 1) out vec2 frag_tex_coord;
layout(location = 2) out vec3 frag_normal;
layout(location = 3) out vec3 frag_tangent;
layout(location = 4) out vec3 frag_bitangent;
layout(location = 5) out uint material_index;

void main()
{
	vec3 vertex_pos = GetVertexPos(push.vb_index, gl_VertexIndex);
	vec2 vertex_tex_coord = GetVertexTexCoord(push.vb_index, gl_VertexIndex);
	vec3 vertex_normal = GetVertexNormal(push.vb_index, gl_VertexIndex);
	vec4 vertex_tangent = GetVertexTangent(push.vb_index, gl_VertexIndex);
	
	mat4 transform = GetInstanceTransform(push.ib_index, gl_InstanceIndex);

	frag_pos = transform * vec4(vertex_pos, 1.0f);
	frag_tex_coord = vertex_tex_coord;

	frag_normal = normalize(transform * vec4(vertex_normal, 0.0f)).xyz;
	frag_tangent = normalize(transform * vec4(vertex_tangent.xyz, 0.0f)).xyz;
	// NOTE: Larger meshes sometimes average tangents for smoothness, so we need to use this trick
	// to re-orthogonalize the tangent (Gram-Schmidt process)
	frag_tangent = normalize(frag_tangent - dot(frag_tangent, frag_normal) * frag_normal);
	frag_bitangent = normalize(cross(frag_normal, frag_tangent)) * (-vertex_tangent.w);

	material_index = GetInstanceMaterialIndex(push.ib_index, gl_InstanceIndex);
	
	gl_Position = camera.proj * camera.view * frag_pos;
}
