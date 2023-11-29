#pragma once
#include "renderer/RenderTypes.h"

#include <vector>

namespace Assets
{

	void Init();
	void Exit();

	void LoadTexture(const std::string& filepath, const std::string& name, TextureFormat format, bool gen_mips, bool is_environment_map);
	TextureHandle_t GetTexture(const std::string& name);

	struct Model
	{
		struct Node
		{
			std::vector<MeshHandle_t> mesh_handles;
			std::vector<MaterialHandle_t> material_handles;
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
