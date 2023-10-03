#version 450
#extension GL_KHR_vulkan_glsl : enable
#extension GL_EXT_nonuniform_qualifier : enable

layout(set = 1, binding = 0) uniform Camera
{
	mat4 view;
	mat4 proj;
} camera[];

layout(push_constant, std140) uniform constants
{
	layout(offset = 0) uint camera_ubo_index;
} push_constants;

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 tex_coord;
// Takes up 4 locations, 2 to 5
layout(location = 2) in mat4 transform;

layout(location = 0) out vec2 frag_tex_coord;

void main()
{
	gl_Position = camera[push_constants.camera_ubo_index].proj * camera[push_constants.camera_ubo_index].view * transform * vec4(position, 1.0);
	frag_tex_coord = tex_coord;
}
