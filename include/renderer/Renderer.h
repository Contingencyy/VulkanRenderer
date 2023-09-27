#pragma once
#include "renderer/RenderTypes.h"

#include <vector>

typedef struct GLFWwindow;

namespace Renderer
{

	void Init(GLFWwindow* window);
	void Exit();
	void BeginFrame(const glm::mat4& view, const glm::mat4& proj);
	void RenderFrame();
	void EndFrame();

	struct CreateTextureArgs
	{
		uint32_t width;
		uint32_t height;
		std::vector<uint8_t> pixels;
	};

	TextureHandle_t CreateTexture(const CreateTextureArgs& args);

	struct CreateMeshArgs
	{
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
	};

	MeshHandle_t CreateMesh(const CreateMeshArgs& args);

	void SubmitMesh(MeshHandle_t mesh_handle, TextureHandle_t texture_handle, const glm::mat4& transform);

}
