#version 450
#pragma once

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
	layout(offset = 4) uint mat_index;
} push_constants;

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 tex_coord;
layout(location = 2) in vec3 normal;
// Takes up 4 locations, 3 to 6
layout(location = 3) in mat4 transform;

layout(location = 0) out vec4 frag_pos;
layout(location = 1) out vec2 frag_tex_coord;
layout(location = 2) out vec3 frag_normal;

void main()
{
	frag_pos = transform * vec4(position, 1.0f);
	frag_tex_coord = tex_coord;
	frag_normal = (transform * vec4(normal, 0.0f)).xyz;
	
	gl_Position = g_camera[push_constants.camera_ubo_index].cam.proj * g_camera[push_constants.camera_ubo_index].cam.view * frag_pos;
}
