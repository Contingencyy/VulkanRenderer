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
		TextureFormat format = TEXTURE_FORMAT_UNDEFINED;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t src_stride = 0;
		std::span<const uint8_t> pixel_bytes;

		bool generate_mips = false;
		bool is_environment_map = false;

		std::string name = "Unnamed";
	};

	TextureHandle_t CreateTexture(const CreateTextureArgs& args);
	void DestroyTexture(TextureHandle_t handle);
	void ImGuiRenderTexture(TextureHandle_t handle, float width, float height);
	void ImGuiRenderTextureButton(TextureHandle_t handle, float width, float height);

	struct CreateMeshArgs
	{
		uint32_t num_vertices = 0;
		uint32_t vertex_stride = 0;
		std::span<const uint8_t> vertices_bytes;

		uint32_t num_indices = 0;
		uint32_t index_stride = 0;
		std::span<const uint8_t> indices_bytes;

		std::string name = "Unnamed";
	};

	MeshHandle_t CreateMesh(const CreateMeshArgs& args);
	void DestroyMesh(MeshHandle_t handle);
	void SubmitMesh(MeshHandle_t mesh_handle, const Assets::Material& material, const glm::mat4& transform);
	
	void SubmitPointlight(const glm::vec3& pos, const glm::vec3& color, float intensity);
	void SubmitAreaLight(TextureHandle_t texture_handle, const glm::mat4& transform, const glm::vec3& color, float intensity, bool two_sided);

}
