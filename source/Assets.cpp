#include "Assets.h"
#include "Common.h"
#include "renderer/Renderer.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf/cgltf.h"

#include <unordered_map>

namespace Assets
{

	struct Data
	{
		std::unordered_map<const char*, TextureHandle_t> textures;
		std::unordered_map<const char*, Model> models;
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

	static void CreateFilepathFromUri(const char* filepath, const char* uri, char** result)
	{
		*result = new char[strlen(filepath) + strlen(uri)];

		cgltf_combine_paths(*result, filepath, uri);
		cgltf_decode_uri(*result + strlen(*result) - strlen(uri));
	}

	template<typename T>
	static T* CGLTFGetDataPointer(const cgltf_accessor* accessor)
	{
		cgltf_buffer_view* buffer_view = accessor->buffer_view;
		uint8_t* base_ptr = (uint8_t*)(buffer_view->buffer->data);
		base_ptr += buffer_view->offset;
		base_ptr += accessor->offset;

		return (T*)base_ptr;
	}

	template<typename T>
	static size_t CGLTFGetIndex(const T* arr, const T* elem)
	{
		return (size_t)(elem - arr);
	}

	static glm::mat4 CGLTFNodeGetTransform(const cgltf_node& node)
	{
		glm::mat4 transform = glm::identity<glm::mat4>();

		if (!node.mesh)
		{
			return transform;
		}

		if (node.has_matrix)
		{
			memcpy(&transform[0][0], &node.matrix[0], sizeof(glm::mat4));
			return transform;
		}

		glm::vec3 translation(0.0);
		glm::quat rotation(0.0, 0.0, 0.0, 0.0);
		glm::vec3 scale(1.0);

		if (node.has_translation)
		{
			translation.x = node.translation[0];
			translation.y = node.translation[1];
			translation.z = node.translation[2];
		}
		if (node.has_rotation)
		{
			rotation.x = node.rotation[0];
			rotation.y = node.rotation[1];
			rotation.z = node.rotation[2];
			rotation.w = node.rotation[3];
		}
		if (node.has_scale)
		{
			scale.x = node.scale[0];
			scale.y = node.scale[1];
			scale.z = node.scale[2];
		}

		transform = glm::translate(transform, translation);
		transform = transform * glm::mat4_cast(rotation);
		transform = glm::scale(transform, scale);
		
		return transform;
	}

	static Model ReadGLTF(const char* filepath)
	{
		cgltf_options options = {};
		/*options.memory.alloc_func = [](void* user, cgltf_size size)
		{
		};
		options.memory.free_func = [](void* user, void* ptr)
		{
		};*/

		cgltf_data* data = nullptr;
		cgltf_result parsed = cgltf_parse_file(&options, filepath, &data);

		if (parsed != cgltf_result_success)
		{
			VK_EXCEPT("Assets", "Failed to load GLTF file: {}", filepath);
		}

		cgltf_load_buffers(&options, data, filepath);

		// Load all texture images upfront
		std::vector<TextureHandle_t> texture_handles(data->images_count);

		for (size_t i = 0; i < data->images_count; ++i)
		{
			char* combined_filepath = nullptr;
			
			// TODO: These should be named after their type and something else to uniquely identify them
			// like URI + BaseColor/Normal/MetallicRoughness
			CreateFilepathFromUri(filepath, data->images[i].uri, &combined_filepath);
			LoadTexture(combined_filepath, data->images[i].uri);
			texture_handles[i] = GetTexture(data->images[i].uri);

			delete combined_filepath;
		}

		// Determine how many meshes we have in total
		size_t num_meshes = 0;
		for (size_t i = 0; i < data->meshes_count; ++i)
		{
			cgltf_mesh* mesh = &data->meshes[i];
			num_meshes += mesh->primitives_count;
		}

		Model model = {};
		std::vector<MeshHandle_t> mesh_handles(num_meshes);
		size_t mesh_handle_index_current = 0;

		for (size_t i = 0; i < data->meshes_count; ++i)
		{
			cgltf_mesh& mesh = data->meshes[i];

			for (size_t j = 0; j < mesh.primitives_count; ++j)
			{
				cgltf_primitive& primitive = mesh.primitives[j];

				// Prepare mesh creation arguments for renderer upload
				Renderer::CreateMeshArgs mesh_args = {};
				mesh_args.indices.resize(primitive.indices->count);

				// Load indices for current primitive
				if (primitive.indices->component_type == cgltf_component_type_r_32u)
				{
					memcpy(mesh_args.indices.data(), CGLTFGetDataPointer<uint32_t>(primitive.indices), sizeof(uint32_t) * primitive.indices->count);
				}
				else if (primitive.indices->component_type == cgltf_component_type_r_16u)
				{
					uint16_t* indices_ptr = CGLTFGetDataPointer<uint16_t>(primitive.indices);

					for (size_t k = 0; k < primitive.indices->count; ++k)
					{
						mesh_args.indices[k] = indices_ptr[k];
					}
				}

				// Load vertices for current primitive
				mesh_args.vertices.resize(primitive.attributes[0].data->count);

				for (size_t k = 0; k < primitive.attributes_count; ++k)
				{
					cgltf_attribute& attribute = primitive.attributes[k];

					switch (attribute.type)
					{
					case cgltf_attribute_type_position:
					{
						glm::vec3* pos_ptr = CGLTFGetDataPointer<glm::vec3>(attribute.data);

						for (size_t l = 0; l < attribute.data->count; ++l)
						{
							mesh_args.vertices[l].pos = pos_ptr[l];
						}
						break;
					}
					case cgltf_attribute_type_texcoord:
					{
						glm::vec2* texcoord_ptr = CGLTFGetDataPointer<glm::vec2>(attribute.data);

						for (size_t l = 0; l < attribute.data->count; ++l)
						{
							mesh_args.vertices[l].tex_coord = texcoord_ptr[l];
						}
						break;
					}
					}
				}

				mesh_handles[mesh_handle_index_current++] = Renderer::CreateMesh(mesh_args);
			}
		}

		model.nodes.resize(data->nodes_count);

		for (size_t i = 0; i < data->nodes_count; ++i)
		{
			cgltf_node& gltf_node = data->nodes[i];
			Model::Node& model_node = model.nodes[i];
			model_node.transform = CGLTFNodeGetTransform(gltf_node);

			model_node.children.resize(gltf_node.children_count);
			for (size_t j = 0; j < gltf_node.children_count; ++j)
			{
				model_node.children[j] = CGLTFGetIndex(data->nodes, gltf_node.children[j]);
			}

			if (gltf_node.mesh)
			{
				model_node.mesh_handles.resize(gltf_node.mesh->primitives_count);
				model_node.texture_handles.resize(gltf_node.mesh->primitives_count);
				model_node.name = gltf_node.name;

				for (size_t j = 0; j < gltf_node.mesh->primitives_count; ++j)
				{
					cgltf_primitive& primitive = gltf_node.mesh->primitives[j];

					size_t mesh_handle_index = CGLTFGetIndex<cgltf_mesh>(data->meshes, gltf_node.mesh) +
						CGLTFGetIndex<cgltf_primitive>(gltf_node.mesh->primitives, &primitive);
					model_node.mesh_handles[j] = mesh_handles[mesh_handle_index];

					if (primitive.material->pbr_metallic_roughness.base_color_texture.texture)
					{
						size_t texture_handle_index = CGLTFGetIndex<cgltf_image>(data->images,
							primitive.material->pbr_metallic_roughness.base_color_texture.texture->image);
						model_node.texture_handles[j] = texture_handles[texture_handle_index];
					}
				}
			}

			if (!gltf_node.parent)
			{
				model.root_nodes.push_back(i);
			}
		}

		cgltf_free(data);
		return model;
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
		args.pixels.resize(image.width * image.height * 4);
		memcpy(args.pixels.data(), image.pixels, args.pixels.size());
		TextureHandle_t texture_handle = Renderer::CreateTexture(args);

		data.textures.emplace(name, texture_handle);
		stbi_image_free(image.pixels);
	}

	TextureHandle_t GetTexture(const char* name)
	{
		auto iter = data.textures.find(name);
		if (iter != data.textures.end())
		{
			return iter->second;
		}

		return TextureHandle_t();
	}

	void LoadGLTF(const char* filepath, const char* name)
	{
		Model model = ReadGLTF(filepath);
		data.models.emplace(name, model);
	}

	Model GetModel(const char* name)
	{
		auto iter = data.models.find(name);
		if (iter != data.models.end())
		{
			return iter->second;
		}

		return Model();
	}

}