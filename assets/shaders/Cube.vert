#version 450

#extension GL_KHR_vulkan_glsl : enable
#extension GL_EXT_nonuniform_qualifier : enable

/*

	This vertex shader projects an equirectangular map onto a unit cube
	Used for processing HDR environment maps for image-based lighting into cubemaps

*/

layout(std140, push_constant) uniform constants
{
	layout(offset = 0) mat4 mvp;
} push_constants;

layout(location = 0) in vec3 position;

layout(location = 0) out vec3 local_position;

void main()
{
	local_position = position;
	gl_Position = push_constants.mvp * vec4(position, 1.0);
}
