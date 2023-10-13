#pragma once
#include "Common.h"
#include "Shared.glsl.h"
#include "renderer/VulkanBackend.h"

using TextureHandle_t = ResourceHandle_t;
using MeshHandle_t = ResourceHandle_t;
using MaterialHandle_t = ResourceHandle_t;

struct Vertex
{
	glm::vec3 pos;
	glm::vec2 tex_coord;
	glm::vec3 normal;
};

struct TextureResource
{
	Vulkan::Image image;
	DescriptorAllocation descriptor;

	~TextureResource()
	{
		Vulkan::DestroyImage(image);
		// TODO: Free descriptors
	}
};

struct MeshResource
{
	Vulkan::Buffer vertex_buffer;
	Vulkan::Buffer index_buffer;

	~MeshResource()
	{
		Vulkan::DestroyBuffer(vertex_buffer);
		Vulkan::DestroyBuffer(index_buffer);
	}
};

struct MaterialResource
{
	TextureHandle_t base_color_texture_handle;
	TextureHandle_t normal_texture_handle;
	TextureHandle_t metallic_roughness_texture_handle;

	MaterialData data;
};
