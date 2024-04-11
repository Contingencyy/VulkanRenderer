#include "Precomp.h"
#include "FileIO.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

namespace FileIO
{

	ReadImageResult ReadImage(const std::filesystem::path& filepath)
	{
		ReadImageResult result = {};

		bool hdr = stbi_is_hdr(filepath.string().c_str());
		stbi_set_flip_vertically_on_load(hdr);
		stbi_info(filepath.string().c_str(), &result.width, &result.height, &result.num_components);

		uint8_t* image_data = nullptr;

		if (hdr)
		{
			image_data = (uint8_t*)stbi_loadf(filepath.string().c_str(), &result.width, &result.height, &result.num_components, STBI_rgb_alpha);
			result.component_size = 4;
		}
		else
		{
			image_data = (uint8_t*)stbi_load(filepath.string().c_str(), &result.width, &result.height, &result.num_components, STBI_rgb_alpha);
			result.component_size = 1;
		}

		if (!image_data)
			LOG_ERR("FileIO::ReadImage", "Failed to read image, data pointer is null");

		result.num_components = 4;
		uint32_t num_total_bytes = result.width * result.height * result.num_components * result.component_size;

		result.pixel_data.resize(num_total_bytes);
		memcpy(result.pixel_data.data(), image_data, num_total_bytes);
		stbi_image_free(image_data);

		return result;
	}

}
