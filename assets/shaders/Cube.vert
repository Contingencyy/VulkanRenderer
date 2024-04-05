#version 460
#include "Common.glsl"

/*

	This vertex shader projects an equirectangular map onto a unit cube
	Used for processing HDR environment maps for image-based lighting into cubemaps

*/

layout(std140, push_constant) uniform PushConsts
{
	layout(offset = 0) mat4 mvp;
	layout(offset = 64) uint vb_index;
} push;

layout(location = 0) out vec3 local_position;

void main()
{
	vec3 vertex_pos = GetVertexPos(push.vb_index, gl_VertexIndex);

	local_position = vertex_pos;
	gl_Position = push.mvp * vec4(vertex_pos, 1.0);
}
