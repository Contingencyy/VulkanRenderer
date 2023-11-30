#pragma once
#include "renderer/RenderTypes.h"

#include <vector>

struct GLFWwindow;

namespace Renderer
{

	void Init(GLFWwindow* window);
	void Exit();

	struct BeginFrameInfo
	{
		glm::mat4 view;
		glm::mat4 proj;

		TextureHandle_t skybox_texture_handle;
	};

	void BeginFrame(const BeginFrameInfo& frame_info);
	void RenderFrame();
	void RenderUI();
	void EndFrame();

	struct CreateTextureArgs
	{
		uint32_t width;
		uint32_t height;
		uint32_t src_stride;
		TextureFormat format;
		std::vector<uint8_t> pixels;

		bool generate_mips;
		bool is_environment_map;
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
		TextureHandle_t base_color_texture_handle;
		TextureHandle_t normal_texture_handle;
		float metallic_factor;
		float roughness_factor;
		TextureHandle_t metallic_roughness_texture_handle;

		bool has_clearcoat;
		float clearcoat_alpha_factor;
		float clearcoat_roughness_factor;
		TextureHandle_t clearcoat_alpha_texture_handle;
		TextureHandle_t clearcoat_normal_texture_handle;
		TextureHandle_t clearcoat_roughness_texture_handle;
	};

	MaterialHandle_t CreateMaterial(const CreateMaterialArgs& args);
	void DestroyMaterial(MaterialHandle_t handle);

	void SubmitMesh(MeshHandle_t mesh_handle, MaterialHandle_t material_handle, const glm::mat4& transform);
	
	void SubmitPointlight(const glm::vec3& pos, const glm::vec3& color, float intensity);

}
