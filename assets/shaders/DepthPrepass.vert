#version 460
#include "Common.glsl"

layout(std140, push_constant) uniform PushConsts
{
	layout(offset = 0) uint ib_index;
	layout(offset = 4) uint vb_index;
} push;

void main()
{
	mat4 transform = GetInstanceTransform(push.ib_index, gl_InstanceIndex);
	vec3 vertex_pos = GetVertexPos(push.vb_index, gl_VertexIndex);

	vec4 world_pos = transform * vec4(vertex_pos, 1.0f);
	gl_Position = camera.proj * camera.view * world_pos;
}
