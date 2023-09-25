#pragma once
#include "renderer/RenderTypes.h"

typedef struct GLFWwindow;

namespace Renderer
{

	void Init(GLFWwindow* window);
	void Exit();
	void RenderFrame();

	struct CreateTextureArgs
	{
		uint32_t width;
		uint32_t height;
		uint8_t* data_ptr;
	};

	ResourceHandle_t CreateTexture(const CreateTextureArgs& args);

	struct CreateMeshArgs
	{
		size_t num_vertices;
		Vertex* vertices;
		size_t num_indices;
		uint32_t* indices;
	};

	ResourceHandle_t CreateMesh(const CreateMeshArgs& args);

}
