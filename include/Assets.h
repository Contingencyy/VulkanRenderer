#pragma once
#include "renderer/RenderTypes.h"

#include <vector>

namespace Assets
{

	void Init();
	void Exit();

	void LoadTexture(const char* filepath, const char* name, TextureFormat format);
	TextureHandle_t GetTexture(const char* name);

	struct Model
	{
		struct Node
		{
			std::vector<MeshHandle_t> mesh_handles;
			std::vector<MaterialHandle_t> material_handles;
			std::vector<uint32_t> children;

			glm::mat4 transform;

			const char* name;
		};

		std::vector<Node> nodes;
		std::vector<uint32_t> root_nodes;

		const char* name;
	};

	void LoadGLTF(const char* filepath, const char* name);
	Model* GetModel(const char* name);

}
