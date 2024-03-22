#pragma once
#include "renderer/RenderTypes.h"

struct GLFWwindow;

namespace Assets
{
	struct Material;
}

namespace Renderer
{

	void Init(GLFWwindow* window, uint32_t window_width, uint32_t window_height);
	void Exit();

	struct BeginFrameInfo
	{
		// The camera's view matrix
		glm::mat4 camera_view;
		// The camera's vertical FOV, in degrees
		float camera_vfov;

		TextureHandle_t skybox_texture_handle;
	};

	void BeginFrame(const BeginFrameInfo& frame_info);
	void RenderFrame();
	void RenderUI();
	void EndFrame();

	struct CreateTextureArgs
	{
		TextureFormat format;
		uint32_t width;
		uint32_t height;
		uint32_t src_stride;
		std::vector<uint8_t> pixels;

		bool generate_mips;
		bool is_environment_map;

		std::string name;
	};

	TextureHandle_t CreateTexture(const CreateTextureArgs& args);
	void DestroyTexture(TextureHandle_t handle);
	void ImGuiRenderTexture(TextureHandle_t handle);

	struct CreateMeshArgs
	{
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;

		std::string name;
	};

	MeshHandle_t CreateMesh(const CreateMeshArgs& args);
	void DestroyMesh(MeshHandle_t handle);
	void SubmitMesh(MeshHandle_t mesh_handle, const Assets::Material& material, const glm::mat4& transform);
	
	void SubmitPointlight(const glm::vec3& pos, const glm::vec3& color, float intensity);

}
