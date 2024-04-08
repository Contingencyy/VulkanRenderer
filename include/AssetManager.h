#pragma once
#include "renderer/RenderTypes.h"

#include <filesystem>

typedef ResourceHandle_t AssetHandle_t;

namespace AssetManager
{

	enum AssetType
	{
		ASSET_TYPE_TEXTURE,
		ASSET_TYPE_MATERIAL,
		ASSET_TYPE_MESH,
		ASSET_TYPE_MODEL,
		ASSET_TYPE_NUM_TYPES
	};

	enum AssetLoadState
	{
		ASSET_LOAD_STATE_NONE,
		ASSET_LOAD_STATE_IMPORTED,
		ASSET_LOAD_STATE_LOADED
	};

	struct Asset
	{
		AssetHandle_t handle;
		AssetType type = ASSET_TYPE_NUM_TYPES;
		AssetLoadState load_state = ASSET_LOAD_STATE_NONE;

		std::filesystem::path filepath;
	};

	struct TextureAsset : Asset
	{
		TextureHandle_t gpu_texture_handle;
	};

	struct MaterialAsset : Asset
	{
		TextureHandle_t albedo_texture_handle;
		TextureHandle_t normal_texture_handle;
		TextureHandle_t metallic_roughness_texture_handle;

		glm::vec4 albedo_factor = glm::vec4(1.0f);
		float metallic_factor = 1.0f;
		float roughness_factor = 1.0f;

		bool has_clearcoat = false;
		TextureHandle_t clearcoat_alpha_texture_handle;
		TextureHandle_t clearcoat_normal_texture_handle;
		TextureHandle_t clearcoat_roughness_texture_handle;

		float clearcoat_alpha_factor = 1.0f;
		float clearcoat_roughness_factor = 1.0f;
	};

	struct ModelAsset : Asset
	{
		struct Node
		{
			std::vector<std::string> mesh_names;
			std::vector<MeshHandle_t> mesh_handles;
			std::vector<MaterialAsset> materials;
			std::vector<uint32_t> children;

			glm::mat4 transform;
		};

		std::vector<Node> nodes;
		std::vector<uint32_t> root_nodes;
	};

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
