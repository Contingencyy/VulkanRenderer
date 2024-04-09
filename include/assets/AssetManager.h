#pragma once
#include "assets/AssetTypes.h"

#include <filesystem>

namespace AssetManager
{

	void Init(const std::filesystem::path& assets_base_path);
	void Exit();

	void RenderUI();

	AssetHandle ImportTexture(const std::filesystem::path& filepath, TextureFormat format, bool gen_mips, bool is_hdr_environment);
	AssetHandle ImportMaterial(std::unique_ptr<MaterialAsset> material_asset);
	AssetHandle ImportModel(const std::filesystem::path& filepath);

	bool IsAssetImported(AssetHandle handle);
	bool IsAssetLoaded(AssetHandle handle);

	template<typename T>
	T* GetAsset(AssetHandle handle);

}
