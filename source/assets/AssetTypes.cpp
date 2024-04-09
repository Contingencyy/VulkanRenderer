#include "Precomp.h"
#include "assets/AssetTypes.h"

#include "imgui/imgui.h"

std::string AssetTypeToString(AssetType type)
{
	switch (type)
	{
	case ASSET_TYPE_TEXTURE: return "Texture";
	case ASSET_TYPE_MATERIAL: return "Material";
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

void ModelAsset::RenderTooltip()
{
	if (ImGui::BeginTooltip())
	{
		ImGui::Text("Type: %s", AssetTypeToString(type).c_str());
		ImGui::Text("Handle: %u", handle);
		ImGui::Text("State: %s", AssetLoadStateToString(load_state).c_str());

		ImGui::EndTooltip();
	}
}
