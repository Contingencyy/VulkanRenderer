#version 450

#extension GL_KHR_vulkan_glsl : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "Shared.glsl.h"

layout(set = DESCRIPTOR_SET_UBO, binding = RESERVED_DESCRIPTOR_UBO_CAMERA) uniform CameraUBO
{
	CameraData camera;
};

layout(location = 0) in vec3 position;

layout(location = 0) out vec3 local_position;

void main()
{
	local_position = position;

	mat4 view_no_translate = mat4(mat3(camera.view));
	vec4 clip_position = camera.proj * view_no_translate * vec4(position, 1.0);

	gl_Position = clip_position.xyww;
}
