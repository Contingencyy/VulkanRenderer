#include "Assets.h"
#include "Common.h"
#include "renderer/Renderer.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf/cgltf.h"

#include "mikkt/mikktspace.h"

#include <unordered_map>

class TangentCalculator
{
public:
	TangentCalculator()
	{
		m_mikkt_interface.m_getNumFaces = GetNumFaces;
		m_mikkt_interface.m_getNumVerticesOfFace = GetNumVerticesOfFace;

		m_mikkt_interface.m_getNormal = GetNormal;
		m_mikkt_interface.m_getPosition = GetPosition;
		m_mikkt_interface.m_getTexCoord = GetTexCoord;
		m_mikkt_interface.m_setTSpaceBasic = SetTSpaceBasic;

		m_mikkt_context.m_pInterface = &m_mikkt_interface;
	}

	void Calculate(Renderer::CreateMeshArgs* mesh)
	{
		m_mikkt_context.m_pUserData = mesh;
		genTangSpaceDefault(&m_mikkt_context);
	}

private:
	static int GetNumFaces(const SMikkTSpaceContext* context)
	{
		Renderer::CreateMeshArgs* mesh = static_cast<Renderer::CreateMeshArgs*>(context->m_pUserData);
		return mesh->indices.size() / 3;
	}

	static int GetVertexIndex(const SMikkTSpaceContext* context, int iFace, int iVert)
	{
		Renderer::CreateMeshArgs* mesh = static_cast<Renderer::CreateMeshArgs*>(context->m_pUserData);

		uint32_t face_size = GetNumVerticesOfFace(context, iFace);
		uint32_t indices_index = (iFace * face_size) + iVert;

		return mesh->indices[indices_index];
	}

	static int GetNumVerticesOfFace(const SMikkTSpaceContext* context, int iFace)
	{
		return 3;
	}

	static void GetPosition(const SMikkTSpaceContext* context, float outpos[], int iFace, int iVert)
	{
		Renderer::CreateMeshArgs* mesh = static_cast<Renderer::CreateMeshArgs*>(context->m_pUserData);

		uint32_t index = GetVertexIndex(context, iFace, iVert);
		const Vertex& vertex = mesh->vertices[index];

		outpos[0] = vertex.pos.x;
		outpos[1] = vertex.pos.y;
		outpos[2] = vertex.pos.z;
	}

	static void GetNormal(const SMikkTSpaceContext* context, float outnormal[], int iFace, int iVert)
	{
		Renderer::CreateMeshArgs* mesh = static_cast<Renderer::CreateMeshArgs*>(context->m_pUserData);

		uint32_t index = GetVertexIndex(context, iFace, iVert);
		const Vertex& vertex = mesh->vertices[index];

		outnormal[0] = vertex.normal.x;
		outnormal[1] = vertex.normal.y;
		outnormal[2] = vertex.normal.z;
	}

	static void GetTexCoord(const SMikkTSpaceContext* context, float outuv[], int iFace, int iVert)
	{
		Renderer::CreateMeshArgs* mesh = static_cast<Renderer::CreateMeshArgs*>(context->m_pUserData);

		uint32_t index = GetVertexIndex(context, iFace, iVert);
		const Vertex& vertex = mesh->vertices[index];

		outuv[0] = vertex.tex_coord.x;
		outuv[1] = vertex.tex_coord.y;
	}

	static void SetTSpaceBasic(const SMikkTSpaceContext* context, const float tangentu[], float fSign, int iFace, int iVert)
	{
		Renderer::CreateMeshArgs* mesh = static_cast<Renderer::CreateMeshArgs*>(context->m_pUserData);

		uint32_t index = GetVertexIndex(context, iFace, iVert);
		Vertex& vertex = mesh->vertices[index];

		vertex.tangent.x = tangentu[0];
		vertex.tangent.y = tangentu[1];
		vertex.tangent.z = tangentu[2];
		vertex.tangent.w = fSign;
	}

private:
	SMikkTSpaceInterface m_mikkt_interface = {};
	SMikkTSpaceContext m_mikkt_context = {};

};

namespace Assets
{

	struct Data
	{
		std::unordered_map<const char*, TextureHandle_t> textures;
		std::unordered_map<const char*, Model> models;

		TangentCalculator tangent_calc;
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

	static TextureHandle_t LoadGLTFTexture(cgltf_image& gltf_image, const char* filepath, TextureFormat format)
	{
		if (VK_RESOURCE_HANDLE_VALID(GetTexture(gltf_image.uri)))
		{
			return GetTexture(gltf_image.uri);
		}

		char* combined_filepath = nullptr;

		CreateFilepathFromUri(filepath, gltf_image.uri, &combined_filepath);
		LoadTexture(combined_filepath, gltf_image.uri, format);
		delete combined_filepath;

		return GetTexture(gltf_image.uri);
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

		cgltf_data* gltf_data = nullptr;
		cgltf_result parsed = cgltf_parse_file(&options, filepath, &gltf_data);

		if (parsed != cgltf_result_success)
		{
			VK_EXCEPT("Assets", "Failed to load GLTF file: {}", filepath);
		}

		cgltf_load_buffers(&options, gltf_data, filepath);

		// Determine how many meshes we have in total
		size_t num_meshes = 0;
		for (size_t i = 0; i < gltf_data->meshes_count; ++i)
		{
			cgltf_mesh* mesh = &gltf_data->meshes[i];
			num_meshes += mesh->primitives_count;
		}

		Model model = {};
		std::vector<MeshHandle_t> mesh_handles(num_meshes);
		size_t mesh_handle_index_current = 0;

		for (size_t i = 0; i < gltf_data->meshes_count; ++i)
		{
			cgltf_mesh& gltf_mesh = gltf_data->meshes[i];

			for (size_t j = 0; j < gltf_mesh.primitives_count; ++j)
			{
				cgltf_primitive& gltf_prim = gltf_mesh.primitives[j];

				// Prepare mesh creation arguments for renderer upload
				Renderer::CreateMeshArgs mesh_args = {};
				mesh_args.indices.resize(gltf_prim.indices->count);

				// Load indices for current primitive
				if (gltf_prim.indices->component_type == cgltf_component_type_r_32u)
				{
					memcpy(mesh_args.indices.data(), CGLTFGetDataPointer<uint32_t>(gltf_prim.indices), sizeof(uint32_t) * gltf_prim.indices->count);
				}
				else if (gltf_prim.indices->component_type == cgltf_component_type_r_16u)
				{
					uint16_t* indices_ptr = CGLTFGetDataPointer<uint16_t>(gltf_prim.indices);

					for (size_t k = 0; k < gltf_prim.indices->count; ++k)
					{
						mesh_args.indices[k] = indices_ptr[k];
					}
				}

				// Load vertices for current primitive
				mesh_args.vertices.resize(gltf_prim.attributes[0].data->count);
				bool calc_tangents = true;

				for (size_t k = 0; k < gltf_prim.attributes_count; ++k)
				{
					cgltf_attribute& attribute = gltf_prim.attributes[k];

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
					case cgltf_attribute_type_normal:
					{
						glm::vec3* normal_ptr = CGLTFGetDataPointer<glm::vec3>(attribute.data);

						for (size_t l = 0; l < attribute.data->count; ++l)
						{
							mesh_args.vertices[l].normal = normal_ptr[l];
						}
						break;
					}
					case cgltf_attribute_type_tangent:
					{
						glm::vec4* tangent_ptr = CGLTFGetDataPointer<glm::vec4>(attribute.data);

						for (size_t l = 0; l < attribute.data->count; ++l)
						{
							mesh_args.vertices[l].tangent = tangent_ptr[l];
						}
						calc_tangents = false;
						break;
					}
					}
				}

				// No tangents found, so we need to calculate them ourselves
				// Bitangents will be made in the shaders to reduce memory bandwidth
				if (calc_tangents)
					data.tangent_calc.Calculate(&mesh_args);

				mesh_handles[mesh_handle_index_current++] = Renderer::CreateMesh(mesh_args);
			}
		}

		// Create all materials
		std::vector<MaterialHandle_t> material_handles(gltf_data->materials_count);

		for (size_t i = 0; i < gltf_data->materials_count; ++i)
		{
			cgltf_material& gltf_material = gltf_data->materials[i];

			Renderer::CreateMaterialArgs material_args = {};
			
			memcpy(&material_args.base_color_factor, gltf_material.pbr_metallic_roughness.base_color_factor, sizeof(glm::vec4));
			// GLTF 2.0 Spec states that:
			// - Base color textures are encoded in SRGB
			// - Normal textures are encoded in linear
			// - Metallic roughness textures are encoded in linear
			if (gltf_material.pbr_metallic_roughness.base_color_texture.texture)
			{
				size_t texture_handle_index = CGLTFGetIndex<cgltf_image>(gltf_data->images,
					gltf_material.pbr_metallic_roughness.base_color_texture.texture->image);
				material_args.base_color_texture_handle = LoadGLTFTexture(gltf_data->images[texture_handle_index], filepath, TextureFormat_RGBA8_SRGB);
			}

			if (gltf_material.normal_texture.texture)
			{
				size_t texture_handle_index = CGLTFGetIndex<cgltf_image>(gltf_data->images,
					gltf_material.normal_texture.texture->image);
				material_args.normal_texture_handle = LoadGLTFTexture(gltf_data->images[texture_handle_index], filepath, TextureFormat_RGBA8_UNORM);
			}

			material_args.metallic_factor = gltf_material.pbr_metallic_roughness.metallic_factor;
			material_args.roughness_factor = gltf_material.pbr_metallic_roughness.roughness_factor;
			if (gltf_material.pbr_metallic_roughness.metallic_roughness_texture.texture)
			{
				size_t texture_handle_index = CGLTFGetIndex<cgltf_image>(gltf_data->images,
					gltf_material.pbr_metallic_roughness.metallic_roughness_texture.texture->image);
				material_args.metallic_roughness_texture_handle = LoadGLTFTexture(gltf_data->images[texture_handle_index], filepath, TextureFormat_RGBA8_UNORM);
			}

			material_handles[i] = Renderer::CreateMaterial(material_args);
		}

		// Create all nodes
		model.nodes.resize(gltf_data->nodes_count);

		for (size_t i = 0; i < gltf_data->nodes_count; ++i)
		{
			cgltf_node& gltf_node = gltf_data->nodes[i];
			Model::Node& model_node = model.nodes[i];
			model_node.transform = CGLTFNodeGetTransform(gltf_node);
			model_node.name = gltf_node.name;

			model_node.children.resize(gltf_node.children_count);
			for (size_t j = 0; j < gltf_node.children_count; ++j)
			{
				model_node.children[j] = CGLTFGetIndex(gltf_data->nodes, gltf_node.children[j]);
			}

			if (gltf_node.mesh)
			{
				model_node.mesh_handles.resize(gltf_node.mesh->primitives_count);
				model_node.material_handles.resize(gltf_node.mesh->primitives_count);

				for (size_t j = 0; j < gltf_node.mesh->primitives_count; ++j)
				{
					cgltf_primitive& primitive = gltf_node.mesh->primitives[j];

					size_t mesh_handle_index = CGLTFGetIndex<cgltf_mesh>(gltf_data->meshes, gltf_node.mesh) +
						CGLTFGetIndex<cgltf_primitive>(gltf_node.mesh->primitives, &primitive);
					model_node.mesh_handles[j] = mesh_handles[mesh_handle_index];

					size_t material_handle_index = CGLTFGetIndex<cgltf_material>(gltf_data->materials, primitive.material);
					model_node.material_handles[j] = material_handles[material_handle_index];
				}
			}

			if (!gltf_node.parent)
			{
				model.root_nodes.push_back(i);
			}
		}

		cgltf_free(gltf_data);
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

	void LoadTexture(const char* filepath, const char* name, TextureFormat format)
	{
		ReadImageResult image = ReadImage(filepath);

		Renderer::CreateTextureArgs args = {};
		args.width = image.width;
		args.height = image.height;
		args.format = format;
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
