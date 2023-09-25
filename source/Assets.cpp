#include "Assets.h"
#include "Common.h"
#include "renderer/Renderer.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf/cgltf.h"

#include <unordered_map>
#include <vector>

namespace Assets
{

	struct Data
	{
		std::unordered_map<const char*, ResourceHandle_t> textures;
		std::unordered_map<const char*, ResourceHandle_t> meshes;
	} data;

	struct ReadImageResult
	{
		uint32_t width;
		uint32_t height;
		uint8_t* pixels;
	};

	static ReadImageResult ReadImage(const char* filepath)
	{
		ReadImageResult result = {};

		int width, height, channels;
		stbi_uc* pixels = stbi_load(filepath, &width, &height, &channels, STBI_rgb_alpha);

		result.width = (uint32_t)width;
		result.height = (uint32_t)height;
		result.pixels = pixels;

		return result;
	}

	struct ReadGLTFResult
	{
		size_t num_vertices;
		Vertex* vertices;
		size_t num_indices;
		uint32_t* indices;
	};

	static ReadGLTFResult ReadGLTF(const char* filepath)
	{
		cgltf_options options = {};
		/*options.memory.alloc_func = [](void* user, cgltf_size size)
		{
		};
		options.memory.free_func = [](void* user, void* ptr)
		{
		};*/

		cgltf_data* data = nullptr;
		cgltf_result result = cgltf_parse_file(&options, filepath, &data);

		if (result != cgltf_result_success)
		{
			VK_EXCEPT("Assets", "Failed to load GLTF file: {}", filepath);
		}

		cgltf_load_buffers(&options, data, filepath);
	}

	void Init()
	{
		// TODO: Load default assets, if necessary
	}

	void Exit()
	{
		// TODO: Release all assets, both on the CPU and GPU
	}

	void LoadTexture(const char* filepath, const char* name)
	{
		ReadImageResult image = ReadImage(filepath);

		Renderer::CreateTextureArgs args = {};
		args.width = image.width;
		args.height = image.height;
		args.data_ptr = image.pixels;
		ResourceHandle_t texture_handle = Renderer::CreateTexture(args);

		data.textures.emplace(name, texture_handle);
		stbi_image_free(image.pixels);
	}

	ResourceHandle_t GetTexture(const char* name)
	{
		auto iter = data.textures.find(name);
		if (iter != data.textures.end())
		{
			return iter->second;
		}

		return ResourceHandle_t();
	}

	void LoadGLTF(const char* filepath, const char* name)
	{
		ReadGLTFResult gltf = ReadGLTF(filepath);

		Renderer::CreateMeshArgs args = {};
		args.num_vertices = gltf.num_vertices;
		args.vertices = gltf.vertices;
		args.num_indices = gltf.num_indices;
		args.indices = gltf.indices;
		ResourceHandle_t mesh_handle = Renderer::CreateMesh(args);

		data.meshes.emplace(name, mesh_handle);
	}

	ResourceHandle_t GetMesh(const char* name)
	{
		auto iter = data.meshes.find(name);
		if (iter != data.meshes.end())
		{
			return iter->second;
		}

		return ResourceHandle_t();
	}

}
