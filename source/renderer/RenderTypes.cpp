#include "Precomp.h"
#include "renderer/RenderTypes.h"

std::string TextureDimensionToString(TextureDimension dim)
{
	switch (dim)
	{
	case TEXTURE_DIMENSION_UNDEFINED: return "Undefined";
	case TEXTURE_DIMENSION_2D: return "Texture2D";
	case TEXTURE_DIMENSION_CUBE: return "TextureCube";
	}

	LOG_ERR("RenderTypes::TextureDimensionToString", "Invalid dimension");
	return "INVALID";
}

std::string TextureFormatToString(TextureFormat format)
{
	switch (format)
	{
		case TEXTURE_FORMAT_UNDEFINED: return "Undefined";
		case TEXTURE_FORMAT_RGBA8_UNORM: return "RGBA8_UNORM";
		case TEXTURE_FORMAT_RGBA8_SRGB: return "RGBA8_SRGB";
		case TEXTURE_FORMAT_RGBA16_SFLOAT: return "RGBA16_SFLOAT";
		case TEXTURE_FORMAT_RGBA32_SFLOAT: return "RGBA32_SFLOAT";
		case TEXTURE_FORMAT_RG16_SFLOAT: return "RG16_SFLOAT";
		case TEXTURE_FORMAT_D32_SFLOAT: return "D32_SFLOAT";
	}

	LOG_ERR("RenderTypes::TextureFormatToString", "Invalid format");
	return "INVALID";
}
