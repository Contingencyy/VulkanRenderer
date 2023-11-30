#version 450

#extension GL_KHR_vulkan_glsl : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "Shared.glsl.h"

layout(set = DESCRIPTOR_SET_UNIFORM_BUFFER, binding = 0) uniform Camera
{
	CameraData cam;
} g_camera[];

layout(std140, push_constant) uniform constants
{
	layout(offset = 0) uint camera_ubo_index;
} push_consts;

layout(location = 0) in vec3 position;

layout(location = 0) out vec3 local_position;

void main()
{
	local_position = position;

	mat4 view_no_translate = mat4(mat3(g_camera[push_consts.camera_ubo_index].cam.view));
	vec4 clip_position = g_camera[push_consts.camera_ubo_index].cam.proj * view_no_translate * vec4(position, 1.0);

	gl_Position = clip_position.xyww;
}
