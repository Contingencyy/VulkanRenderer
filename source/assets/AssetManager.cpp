#include "Precomp.h"
#include "assets/AssetManager.h"
#include "assets/AssetImporter.h"
#include "renderer/Renderer.h"

#include "imgui/imgui.h"

namespace AssetManager
{

	struct Data
	{
		std::filesystem::path assets_base_dir;
		std::filesystem::path models_base_dir;
		std::filesystem::path textures_base_dir;

		std::unordered_map<AssetHandle, std::unique_ptr<Asset>> assets;

		std::filesystem::path current_dir;
		ImVec2 asset_thumbnail_base_size = { 128.0f, 128.0f };
		ImVec2 asset_thumbnail_base_padding = { 16.0f, 16.0f };
	} static *data;

	static void RenderAssetBrowserUI()
	{
		if (ImGui::BeginMenuBar())
		{
			std::filesystem::path new_path = data->current_dir;
			std::filesystem::path level_path;

			for (std::filesystem::path level : data->current_dir)
			{
				level_path /= level;

				ImGui::Text(level.string().c_str());
				if (ImGui::IsItemClicked())
				{
					new_path = level_path;
				}
				ImGui::Text("\\");
			}

			data->current_dir = new_path;

			ImGui::EndMenuBar();
		}

		float thumbnail_width = data->asset_thumbnail_base_size.x + data->asset_thumbnail_base_padding.x;
		float content_width = ImGui::GetContentRegionAvail().x;
		int32_t num_columns = std::max(static_cast<int32_t>(content_width / thumbnail_width), 1);

		ImGui::Columns(num_columns, 0, false);

		for (const auto& item : std::filesystem::directory_iterator(data->current_dir))
		{
			std::filesystem::path relative_path = std::filesystem::relative(item.path(), data->assets_base_dir);
			std::string filename = relative_path.filename().string();

			AssetHandle handle = AssetImporter::MakeAssetHandleFromFilepath(item.path());
			RenderResourceHandle preview_texture_render_handle;

			auto iter = data->assets.find(handle);
			if (iter != data->assets.end())
				preview_texture_render_handle = iter->second->preview_texture_render_handle;

			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
			Renderer::ImGuiImageButton(preview_texture_render_handle, data->asset_thumbnail_base_size.x, data->asset_thumbnail_base_size.y);
			ImGui::PopStyleColor();

			if (ImGui::IsItemHovered())
			{
				if (iter != data->assets.end())
					iter->second->RenderTooltip();

				if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
					item.is_directory())
				{
					data->current_dir /= item.path().filename();
				}
			}
			ImGui::TextWrapped(filename.c_str());

			ImGui::NextColumn();
		}

		ImGui::Columns();
	}

	void Init(const std::filesystem::path& assets_base_path)
	{
		data = new Data();

		data->assets_base_dir = assets_base_path;
		data->models_base_dir = data->assets_base_dir.string() + "\\models";
		data->textures_base_dir = assets_base_path.string() + "\\textures";

		data->current_dir = data->assets_base_dir;
	}

	void Exit()
	{
		delete data;
		data = nullptr;
	}

	void RenderUI()
	{
		if (ImGui::Begin("Asset Manager", nullptr, ImGuiWindowFlags_MenuBar))
		{
			RenderAssetBrowserUI();
		}
		ImGui::End();
	}

	AssetHandle ImportTexture(const std::filesystem::path& filepath, TextureFormat format, bool gen_mips, bool is_hdr_environment)
	{
		std::unique_ptr<TextureAsset> texture_asset = std::move(AssetImporter::ImportTexture(filepath, format, gen_mips, is_hdr_environment));
		AssetHandle handle = texture_asset->handle;

		data->assets.insert({ handle, std::move(texture_asset) });
		return handle;
	}

	AssetHandle ImportMaterial(std::unique_ptr<MaterialAsset> material_asset)
	{
		AssetHandle handle = material_asset->handle;
		data->assets.insert({ material_asset->handle, std::move(material_asset) });

		return handle;
	}

	AssetHandle ImportModel(const std::filesystem::path& filepath)
	{
		std::unique_ptr<ModelAsset> model_asset = std::move(AssetImporter::ImportModel(filepath));
		AssetHandle handle = model_asset->handle;

		data->assets.insert({ model_asset->handle, std::move(model_asset) });
		return handle;
	}

	bool IsAssetImported(AssetHandle handle)
	{
		return data->assets.find(handle) != data->assets.end();
	}

	bool IsAssetLoaded(AssetHandle handle)
	{
		if (!IsAssetImported)
			return false;

		Asset* asset = data->assets.at(handle).get();
		return asset->load_state == ASSET_LOAD_STATE_LOADED;
	}

	template<>
	TextureAsset* GetAsset(AssetHandle handle)
	{
		if (!IsAssetImported(handle))
			return nullptr;

		TextureAsset* asset = dynamic_cast<TextureAsset*>(data->assets.at(handle).get());
		if (!IsAssetLoaded(handle))
			AssetImporter::LoadTexture(*asset);

		return asset;
	}

	template<>
	ModelAsset* GetAsset(AssetHandle handle)
	{
		if (!IsAssetImported(handle))
			return nullptr;

		ModelAsset* asset = dynamic_cast<ModelAsset*>(data->assets.at(handle).get());
		if (!IsAssetLoaded(handle))
			AssetImporter::LoadModel(*asset);

		return asset;
	}

}
