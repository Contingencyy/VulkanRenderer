#pragma once

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

struct TextureViewCreateInfo
{
	TextureFormat format = TEXTURE_FORMAT_UNDEFINED;
	TextureDimension dimension = TEXTURE_DIMENSION_UNDEFINED;

	uint32_t base_mip = 0;
	uint32_t num_mips = UINT32_MAX;
	uint32_t base_layer = 0;
	uint32_t num_layers = UINT32_MAX;

	bool operator==(const TextureViewCreateInfo& other) const
	{
		return (
			format == other.format &&
			dimension == other.dimension &&
			base_mip == other.base_mip &&
			num_mips == other.num_mips &&
			base_layer == other.base_layer &&
			num_layers == other.num_layers
		);
	}
};

template<>
struct std::hash<TextureViewCreateInfo>
{
	std::size_t operator()(const TextureViewCreateInfo& view_info) const
	{
		return (
			std::hash<uint32_t>()(view_info.format) ^
			(std::hash<uint32_t>()(view_info.dimension) << 10) ^
			(std::hash<uint32_t>()(view_info.base_mip) << 20) ^
			(std::hash<uint32_t>()(view_info.num_mips) << 30) ^
			(std::hash<uint32_t>()(view_info.base_layer) << 40) ^
			(std::hash<uint32_t>()(view_info.num_layers) << 50)
		);
	}
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
	BUFFER_USAGE_COPY_DST = (1 << 7),
	BUFFER_USAGE_RESOURCE_DESCRIPTORS = (1 << 8),
	BUFFER_USAGE_SAMPLER_DESCRIPTORS = (1 << 9)
};

struct BufferCreateInfo
{
	Flags usage_flags = BUFFER_USAGE_NONE;
	Flags memory_flags = GPU_MEMORY_DEVICE_LOCAL;
	uint64_t size_in_bytes = 0;

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
	SAMPLER_FILTER_LINEAR,
	SAMPLER_FILTER_CUBIC
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
