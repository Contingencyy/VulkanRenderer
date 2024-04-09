#pragma once
#include "assets/AssetTypes.h"

namespace AssetImporter
{

	AssetHandle MakeAssetHandleFromFilepath(const std::filesystem::path& filepath);
	AssetType GetAssetTypeFromFileExtension(const std::filesystem::path& filepath);

	std::unique_ptr<TextureAsset> ImportTexture(const std::filesystem::path& filepath, TextureFormat format, bool gen_mips, bool is_environment_map);
	void LoadTexture(TextureAsset& texture_asset);

	std::unique_ptr<ModelAsset> ImportModel(const std::filesystem::path& filepath);
	void LoadModel(ModelAsset& model_asset);

}
