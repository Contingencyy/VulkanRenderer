#pragma once
#include "assets/AssetTypes.h"

#include <filesystem>

namespace AssetManager
{

	void Init(const std::filesystem::path& assets_base_path);
	void Exit();

	void RenderUI();

	AssetHandle_t LoadTexture(const std::filesystem::path& filepath, TextureFormat format, bool gen_mips, bool is_environment_map);
	AssetHandle_t LoadGLTF(const std::filesystem::path& filepath);

	Asset* GetAssetEx(AssetHandle_t handle);
	template<typename T>
	T* GetAsset(AssetHandle_t handle)
	{
		return static_cast<T*>(GetAssetEx(handle));
	}

}
