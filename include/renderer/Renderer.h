#pragma once
#include "renderer/RenderTypes.h"

#include <vector>

struct GLFWwindow;

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
	void DestroyTexture(TextureHandle_t handle);

	struct CreateMeshArgs
	{
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
	};

	MeshHandle_t CreateMesh(const CreateMeshArgs& args);
	void DestroyMesh(MeshHandle_t handle);

	struct CreateMaterialArgs
	{
		glm::vec4 base_color_factor;
		TextureHandle_t base_color_handle;
	};

	MaterialHandle_t CreateMaterial(const CreateMaterialArgs& args);
	void DestroyMaterial(MaterialHandle_t handle);

	void SubmitMesh(MeshHandle_t mesh_handle, MaterialHandle_t material_handle, const glm::mat4& transform);

}
