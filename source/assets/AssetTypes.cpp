#include "Precomp.h"
#include "assets/AssetTypes.h"

#include "imgui/imgui.h"

std::string AssetTypeToString(AssetType type)
{
	switch (type)
	{
	case ASSET_TYPE_TEXTURE: return "Texture";
	case ASSET_TYPE_MATERIAL: return "Material";
	case ASSET_TYPE_MESH: return "Mesh";
	case ASSET_TYPE_MODEL: return "Model";
	}

	LOG_ERR("AssetManager::AssetTypeToString", "Invalid asset type");
	return "INVALID";
}

std::string AssetLoadStateToString(AssetLoadState load_state)
{
	switch (load_state)
	{
	case ASSET_LOAD_STATE_NONE: return "Not loaded";
	case ASSET_LOAD_STATE_IMPORTED: return "Imported";
	case ASSET_LOAD_STATE_LOADED: return "Loaded";
	}

	LOG_ERR("AssetManager::AssetLoadStateToString", "Invalid load state");
	return "INVALID";
}

void TextureAsset::RenderTooltip()
{
	if (ImGui::BeginTooltip())
	{
		ImGui::Text("Type: %s", AssetTypeToString(type).c_str());
		ImGui::Text("Handle: %u", handle);
		ImGui::Text("State: %s", AssetLoadStateToString(load_state).c_str());

		ImGui::Text("Render handle: %u", texture_render_handle);
		ImGui::Text("Format: %s", TextureFormatToString(format).c_str());
		ImGui::Text("Generate mips: %s", mips ? "true" : "false");
		ImGui::Text("Environment map: %s", is_environment_map ? "true" : "false");

		ImGui::EndTooltip();
	}
}

void TextureAsset::RenderUI()
{
}

void MaterialAsset::RenderTooltip()
{
	if (ImGui::BeginTooltip())
	{
		ImGui::Text("Type: %s", AssetTypeToString(type).c_str());
		ImGui::Text("Handle: %u", handle);
		ImGui::Text("State: %s", AssetLoadStateToString(load_state).c_str());

		ImGui::EndTooltip();
	}
}

void MaterialAsset::RenderUI()
{
}

void MeshAsset::RenderTooltip()
{
	if (ImGui::BeginTooltip())
	{
		ImGui::Text("Type: %s", AssetTypeToString(type).c_str());
		ImGui::Text("Handle: %u", handle);
		ImGui::Text("State: %s", AssetLoadStateToString(load_state).c_str());

		ImGui::Text("Vertex count: %u", num_vertices);
		ImGui::Text("Index count: %u", num_indices);
		ImGui::Text("Triangle count: %u", num_triangles);

		ImGui::EndTooltip();
	}
}

void MeshAsset::RenderUI()
{
}

void ModelAsset::RenderTooltip()
{
	if (ImGui::BeginTooltip())
	{
		ImGui::Text("Type: %s", AssetTypeToString(type).c_str());
		ImGui::Text("Handle: %u", handle);
		ImGui::Text("State: %s", AssetLoadStateToString(load_state).c_str());

		ImGui::Text("Mesh count: %u", mesh_assets.size());
		ImGui::Text("Material count: %u", material_assets.size());

		ImGui::EndTooltip();
	}
}

void ModelAsset::RenderUI()
{
}
