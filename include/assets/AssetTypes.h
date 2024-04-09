#pragma once
#include "renderer/RenderTypes.h"

#include <filesystem>

using AssetHandle = ResourceHandle_t;

enum AssetType
{
	ASSET_TYPE_TEXTURE,
	ASSET_TYPE_MATERIAL,
	ASSET_TYPE_MODEL,
	ASSET_TYPE_NUM_TYPES
};

enum AssetLoadState
{
	ASSET_LOAD_STATE_NONE,
	ASSET_LOAD_STATE_IMPORTED,
	ASSET_LOAD_STATE_LOADED
};

std::string AssetTypeToString(AssetType type);
std::string AssetLoadStateToString(AssetLoadState load_state);

struct Asset
{
	virtual ~Asset() {}
	
	virtual void RenderTooltip() = 0;

	AssetType type = ASSET_TYPE_NUM_TYPES;
	AssetHandle handle;
	AssetLoadState load_state = ASSET_LOAD_STATE_NONE;

	std::filesystem::path filepath;
	RenderResourceHandle preview_texture_render_handle;
};

struct TextureAsset : Asset
{
	virtual ~TextureAsset() {}

	virtual void RenderTooltip();

	RenderResourceHandle texture_render_handle;
	TextureFormat format = TEXTURE_FORMAT_UNDEFINED;
	bool mips = true;
	bool is_environment_map = false;
};

struct MaterialAsset : Asset
{
	virtual ~MaterialAsset() {}

	virtual void RenderTooltip();

	RenderResourceHandle tex_albedo_render_handle;
	RenderResourceHandle tex_normal_render_handle;
	RenderResourceHandle tex_metal_rough_render_handle;

	glm::vec4 albedo_factor = glm::vec4(1.0f);
	float metallic_factor = 1.0f;
	float roughness_factor = 1.0f;

	bool has_clearcoat = false;
	RenderResourceHandle tex_cc_alpha_render_handle;
	RenderResourceHandle tex_cc_normal_render_handle;
	RenderResourceHandle tex_cc_rough_render_handle;

	float clearcoat_alpha_factor = 1.0f;
	float clearcoat_roughness_factor = 1.0f;
};

struct ModelAsset : Asset
{
	virtual ~ModelAsset() {}

	virtual void RenderTooltip();

	struct Node
	{
		std::vector<std::string> mesh_names;
		std::vector<RenderResourceHandle> mesh_render_handles;
		std::vector<MaterialAsset> materials;
		std::vector<uint32_t> children;

		glm::mat4 transform;
	};

	std::vector<Node> nodes;
	std::vector<uint32_t> root_nodes;
};
