#pragma once
#include "Common.h"

using TextureHandle_t = ResourceHandle_t;
using MeshHandle_t = ResourceHandle_t;

struct Vertex
{
	glm::vec3 pos;
	glm::vec2 tex_coord;
	glm::vec3 normal;
	glm::vec4 tangent;
};

/*
	----------------------------------------------------------------------------------------------
	--------------------------------------- Texture ----------------------------------------------
	----------------------------------------------------------------------------------------------
*/

typedef uint32_t Flags;

class Texture;

enum TextureDimension
{
	TEXTURE_DIMENSION_UNDEFINED,
	TEXTURE_DIMENSION_2D,
	TEXTURE_DIMENSION_CUBE
};

enum TextureFormat
{
	TEXTURE_FORMAT_UNDEFINED,
	TEXTURE_FORMAT_RGBA8_UNORM,
	TEXTURE_FORMAT_RGBA8_SRGB,
	TEXTURE_FORMAT_RGBA16_SFLOAT,
	TEXTURE_FORMAT_RGBA32_SFLOAT,
	TEXTURE_FORMAT_RG16_SFLOAT,
	TEXTURE_FORMAT_D32_SFLOAT
};

enum TextureUsageFlags
{
	TEXTURE_USAGE_NONE,
	TEXTURE_USAGE_RENDER_TARGET = (1 << 0),
	TEXTURE_USAGE_DEPTH_TARGET = (1 << 1),
	TEXTURE_USAGE_DEPTH_STENCIL_TARGET = (1 << 2),
	TEXTURE_USAGE_SAMPLED = (1 << 3),
	TEXTURE_USAGE_READ_ONLY = (1 << 4),
	TEXTURE_USAGE_READ_WRITE = (1 << 5),
	TEXTURE_USAGE_COPY_SRC = (1 << 6),
	TEXTURE_USAGE_COPY_DST = (1 << 7)
};

inline bool IsHDRFormat(TextureFormat format)
{
	switch (format)
	{
	case TEXTURE_FORMAT_RGBA8_UNORM:
	case TEXTURE_FORMAT_RGBA8_SRGB:
	case TEXTURE_FORMAT_D32_SFLOAT:
		return false;
	case TEXTURE_FORMAT_RGBA16_SFLOAT:
	case TEXTURE_FORMAT_RGBA32_SFLOAT:
	case TEXTURE_FORMAT_RG16_SFLOAT:
		return true;
	}
}

struct TextureCreateInfo
{
	TextureFormat format = TEXTURE_FORMAT_UNDEFINED;
	Flags usage_flags = TEXTURE_USAGE_NONE;
	TextureDimension dimension = TEXTURE_DIMENSION_UNDEFINED;

	uint32_t width = 0;
	uint32_t height = 0;

	uint32_t num_mips = 1;
	uint32_t num_layers = 1;

	std::string name = "Unnamed Texture";
};

/*
	----------------------------------------------------------------------------------------------
	--------------------------------------- Buffer -----------------------------------------------
	----------------------------------------------------------------------------------------------
*/

class Buffer;

enum GPUMemoryFlags
{
	GPU_MEMORY_DEVICE_LOCAL = 0,
	GPU_MEMORY_HOST_VISIBLE = (1 << 0),
	GPU_MEMORY_HOST_COHERENT = (1 << 1)
};

enum BufferUsageFlags
{
	BUFFER_USAGE_NONE = 0,
	BUFFER_USAGE_STAGING = (1 << 0),
	BUFFER_USAGE_UNIFORM = (1 << 1),
	BUFFER_USAGE_VERTEX = (1 << 2),
	BUFFER_USAGE_INDEX = (1 << 3),
	BUFFER_USAGE_READ_ONLY = (1 << 4),
	BUFFER_USAGE_READ_WRITE = (1 << 5),
	BUFFER_USAGE_COPY_SRC = (1 << 6),
	BUFFER_USAGE_COPY_DST = (1 << 7)
};

struct BufferCreateInfo
{
	Flags usage_flags = BUFFER_USAGE_NONE;
	Flags memory_flags = GPU_MEMORY_DEVICE_LOCAL;
	size_t size_in_bytes = 0;

	std::string name = "Unnamed Buffer";
};

/*
	----------------------------------------------------------------------------------------------
	--------------------------------------- Sampler ----------------------------------------------
	----------------------------------------------------------------------------------------------
*/

class Sampler;

enum SamplerAddressMode
{
	SAMPLER_ADDRESS_MODE_REPEAT,
	SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
	SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
	SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
	SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE
};

enum SamplerBorderColor
{
	SAMPLER_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
	SAMPLER_BORDER_COLOR_INT_TRANSPARENT_BLACK,
	SAMPLER_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
	SAMPLER_BORDER_COLOR_INT_OPAQUE_BLACK,
	SAMPLER_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
	SAMPLER_BORDER_COLOR_INT_OPAQUE_WHITE
};

enum SamplerFilter
{
	SAMPLER_FILTER_NEAREST,
	SAMPLER_FILTER_LINEAR
};

struct SamplerCreateInfo
{
	SamplerAddressMode address_u = SAMPLER_ADDRESS_MODE_REPEAT;
	SamplerAddressMode address_v = SAMPLER_ADDRESS_MODE_REPEAT;
	SamplerAddressMode address_w = SAMPLER_ADDRESS_MODE_REPEAT;
	SamplerBorderColor border_color = SAMPLER_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;

	SamplerFilter filter_min = SAMPLER_FILTER_NEAREST;
	SamplerFilter filter_mag = SAMPLER_FILTER_NEAREST;
	SamplerFilter filter_mip = SAMPLER_FILTER_NEAREST;

	bool enable_anisotropy = false;

	float min_lod = 0.0f;
	float max_lod = std::numeric_limits<float>::max();

	std::string name = "Unnamed Sampler";
};

//struct SamplerResource
//{
//	VkSampler sampler;
//	DescriptorAllocation descriptor;
//
//	~SamplerResource()
//	{
//		vkDestroySampler(vk_inst.device, sampler, nullptr);
//		// TODO: Free descriptors
//	}
//};
//
//struct TextureResource
//{
//	Vulkan::Image image;
//	Vulkan::ImageView view;
//	DescriptorAllocation descriptor;
//
//	// We need this descriptor set to render any texture to ImGui menus
//	VkDescriptorSet imgui_descriptor_set;
//
//	// TODO: Find a better solution for this, for now this is used for hdr environment maps and points to the
//	// irradiance map made from this hdr environment map
//	TextureHandle_t next;
//
//	~TextureResource()
//	{
//		Vulkan::DestroyImageView(view);
//		Vulkan::DestroyImage(image);
//		// TODO: Free descriptors
//	}
//};
//
//struct MeshResource
//{
//	Vulkan::Buffer vertex_buffer;
//	Vulkan::Buffer index_buffer;
//
//	~MeshResource()
//	{
//		Vulkan::DestroyBuffer(vertex_buffer);
//		Vulkan::DestroyBuffer(index_buffer);
//	}
//};
