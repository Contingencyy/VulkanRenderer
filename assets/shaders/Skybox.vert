#version 460
#include "Common.glsl"

layout(std140, push_constant) uniform PushConsts
{
	layout(offset = 0) uint vb_index;
} push;

layout(location = 0) out vec3 local_position;

void main()
{
	vec3 vertex_pos = GetVertexPos(push.vb_index, gl_VertexIndex);
	
	local_position = vertex_pos;

	mat4 view_no_translate = mat4(mat3(camera.view));
	vec4 clip_position = camera.proj * view_no_translate * vec4(vertex_pos, 1.0);

	gl_Position = clip_position.xyww;
}
