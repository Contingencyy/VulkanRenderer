#pragma once
#include "renderer/RenderTypes.h"

namespace Assets
{

	void Init();
	void Exit();

	void LoadTexture(const std::string& filepath, const std::string& name, TextureFormat format, bool gen_mips, bool is_environment_map);
	TextureHandle_t GetTexture(const std::string& name);

	struct Material
	{
		TextureHandle_t albedo_texture_handle;
		TextureHandle_t normal_texture_handle;
		TextureHandle_t metallic_roughness_texture_handle;

		glm::vec4 albedo_factor;
		float metallic_factor;
		float roughness_factor;

		bool has_clearcoat;
		TextureHandle_t clearcoat_alpha_texture_handle;
		TextureHandle_t clearcoat_normal_texture_handle;
		TextureHandle_t clearcoat_roughness_texture_handle;

		float clearcoat_alpha_factor;
		float clearcoat_roughness_factor;
	};

	struct Model
	{
		struct Node
		{
			std::vector<MeshHandle_t> mesh_handles;
			std::vector<Material> materials;
			std::vector<uint32_t> children;

			glm::mat4 transform;

			std::string name;
		};

		std::vector<Node> nodes;
		std::vector<uint32_t> root_nodes;

		std::string name;
	};

	void LoadGLTF(const std::string& filepath, const std::string& name);
	Model* GetModel(const std::string& name);

}
