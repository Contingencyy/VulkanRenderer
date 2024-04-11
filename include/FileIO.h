#pragma once
#include <filesystem>

namespace FileIO
{

	struct ReadImageResult
	{
		int32_t width;
		int32_t height;
		int32_t num_components;
		int32_t component_size;

		std::vector<uint8_t> pixel_data;
	};

	ReadImageResult ReadImage(const std::filesystem::path& filepath);
	//void WriteImage(const std::filesystem::path& filepath);

}
