#pragma once
#include "Common.h"
#include "Shared.glsl.h"
#include "renderer/VulkanBackend.h"

using TextureHandle_t = ResourceHandle_t;
using MeshHandle_t = ResourceHandle_t;
using MaterialHandle_t = ResourceHandle_t;
using SamplerHandle_t = ResourceHandle_t;

struct Vertex
{
	glm::vec3 pos;
	glm::vec2 tex_coord;
	glm::vec3 normal;
	glm::vec4 tangent;
};

enum TextureFormat
{
	TextureFormat_RGBA8_UNORM,
	TextureFormat_RGBA8_SRGB,
	TextureFormat_RGBA32_SFLOAT
};

static const std::vector<VkFormat> TEXTURE_FORMAT_TO_VK_FORMAT = { VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_R32G32B32A32_SFLOAT };

static bool IsHDRFormat(TextureFormat format)
{
	switch (format)
	{
	case TextureFormat_RGBA8_UNORM:
	case TextureFormat_RGBA8_SRGB:
		return false;
	case TextureFormat_RGBA32_SFLOAT:
		return true;
	}
}

struct SamplerResource
{
	VkSampler sampler;
	DescriptorAllocation descriptor;

	~SamplerResource()
	{
		vkDestroySampler(vk_inst.device, sampler, nullptr);
		// TODO: Free descriptors
	}
};

struct TextureResource
{
	Vulkan::Image image;
	Vulkan::ImageView view;
	DescriptorAllocation descriptor;

	SamplerHandle_t sampler;

	// We need this descriptor set to render any texture to ImGui menus
	VkDescriptorSet imgui_descriptor_set;

	// TODO: Find a better solution for this, for now this is used for hdr environment maps and points to the
	// irradiance map made from this hdr environment map
	TextureHandle_t next;

	~TextureResource()
	{
		Vulkan::DestroyImageView(view);
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
	
	TextureHandle_t clearcoat_alpha_texture_handle;
	TextureHandle_t clearcoat_normal_texture_handle;
	TextureHandle_t clearcoat_roughness_texture_handle;

	MaterialData data;
};
