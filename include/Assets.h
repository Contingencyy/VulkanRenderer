#pragma once
#include "renderer/RenderTypes.h"

namespace Assets
{

	struct Material
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

	struct Model
	{
		struct Node
		{
			std::vector<std::string> mesh_names;
			std::vector<MeshHandle_t> mesh_handles;
			std::vector<Material> materials;
			std::vector<uint32_t> children;

			glm::mat4 transform;
		};

		std::vector<Node> nodes;
		std::vector<uint32_t> root_nodes;

		std::string name;
	};

	void Init(const std::filesystem::path& assets_base_path);
	void Exit();

	void RenderUI();

	void LoadTexture(const std::filesystem::path& filepath, TextureFormat format, bool gen_mips, bool is_environment_map);
	TextureHandle_t GetTexture(const std::filesystem::path& filepath);

	void LoadGLTF(const std::filesystem::path& filepath);
	Model* GetModel(const std::filesystem::path& filepath);

}
