#version 450
#extension GL_KHR_vulkan_glsl : enable
#extension GL_EXT_nonuniform_qualifier : enable

layout(set = 0, binding = 0) uniform Scene
{
	mat4 model;
	mat4 view;
	mat4 proj;
} scene[];

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;
layout(location = 2) in vec2 tex_coord;

layout(location = 0) out vec3 frag_color;
layout(location = 1) out vec2 frag_tex_coord;

void main()
{
	gl_Position = scene[0].proj * scene[0].view * scene[0].model * vec4(position, 1.0);
	frag_color = color;
	frag_tex_coord = tex_coord;
}
