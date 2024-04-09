#pragma once
#include "renderer/RenderTypes.h"

struct GLFWwindow;
struct MaterialAsset;

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

		RenderResourceHandle skybox_texture_handle;
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

	RenderResourceHandle CreateTexture(const CreateTextureArgs& args);
	void DestroyTexture(RenderResourceHandle handle);
	void ImGuiImage(RenderResourceHandle texture_handle, float width, float height);
	void ImGuiImageButton(RenderResourceHandle texture_handle, float width, float height);

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

	RenderResourceHandle CreateMesh(const CreateMeshArgs& args);
	void DestroyMesh(RenderResourceHandle handle);
	void SubmitMesh(RenderResourceHandle mesh_handle, const MaterialAsset& material, const glm::mat4& transform);
	
	void SubmitPointlight(const glm::vec3& pos, const glm::vec3& color, float intensity);
	void SubmitAreaLight(RenderResourceHandle texture_handle, const glm::mat4& transform, const glm::vec3& color, float intensity, bool two_sided);

}
