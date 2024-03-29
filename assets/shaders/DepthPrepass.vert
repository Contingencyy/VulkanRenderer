#version 460

#include "Common.glsl"

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 tex_coord;
layout(location = 2) in vec3 normal;
layout(location = 3) in vec4 tangent;
// Takes up 4 locations, 5 to 8
layout(location = 4) in mat4 transform;

void main()
{
	vec4 world_pos = transform * vec4(position, 1.0f);
	gl_Position = camera.proj * camera.view * world_pos;
}
