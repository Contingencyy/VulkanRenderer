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

#include "renderer/ResourceSlotmap.h"

namespace Assets
{

	struct Data
	{
		std::unordered_map<std::string, TextureHandle_t> textures;
		std::unordered_map<std::string, Model> models;
		TangentCalculator tangent_calc;
	} data;

	struct ReadImageResult
	{
		int32_t width;
		int32_t height;
		int32_t num_components;
		int32_t component_size;

		uint8_t* pixels;
	};

	static ReadImageResult ReadImage(const std::string& filepath, bool hdr)
	{
		ReadImageResult result = {};
		stbi_set_flip_vertically_on_load(hdr);

		if (hdr)
		{
			result.pixels = (uint8_t*)stbi_loadf(filepath.c_str(), &result.width, &result.height, &result.num_components, STBI_rgb_alpha);
			// We force stbi to load rgba values, so we always have 4 components
			result.num_components = 4;
			result.component_size = 4;
		}
		else
		{
			result.pixels = (uint8_t*)stbi_load(filepath.c_str(), &result.width, &result.height, &result.num_components, STBI_rgb_alpha);
			// We force stbi to load rgba values, so we always have 4 components
			result.num_components = 4;
			result.component_size = 1;
		}

		return result;
	}

	static void CreateFilepathFromUri(const std::string& filepath, const std::string& uri, char** result)
	{
		*result = new char[filepath.size() + uri.size()];

		cgltf_combine_paths(*result, filepath.c_str(), uri.c_str());
		cgltf_decode_uri(*result + strlen(*result) - uri.size());
	}

	static TextureHandle_t LoadGLTFTexture(cgltf_image& gltf_image, const std::string& filepath, TextureFormat format)
	{
		if (VK_RESOURCE_HANDLE_VALID(GetTexture(gltf_image.uri)))
		{
			return GetTexture(gltf_image.uri);
		}

		char* combined_filepath = nullptr;

		CreateFilepathFromUri(filepath, gltf_image.uri, &combined_filepath);
		LoadTexture(combined_filepath, gltf_image.uri, format, true, false);
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

	static Model ReadGLTF(const std::string& filepath)
	{
		cgltf_options options = {};
		/*options.memory.alloc_func = [](void* user, cgltf_size size)
		{
		};
		options.memory.free_func = [](void* user, void* ptr)
		{
		};*/

		cgltf_data* gltf_data = nullptr;
		cgltf_result parsed = cgltf_parse_file(&options, filepath.c_str(), &gltf_data);

		if (parsed != cgltf_result_success)
		{
			VK_EXCEPT("Assets", "Failed to load GLTF file: {}", filepath);
		}

		cgltf_load_buffers(&options, gltf_data, filepath.c_str());

		// Determine how many meshes we have in total
		size_t num_meshes = 0;
		for (size_t i = 0; i < gltf_data->meshes_count; ++i)
		{
			cgltf_mesh* mesh = &gltf_data->meshes[i];
			num_meshes += mesh->primitives_count;
		}

		Model model = {};
		model.name = filepath;
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
		std::vector<Material> materials(gltf_data->materials_count);

		for (size_t i = 0; i < gltf_data->materials_count; ++i)
		{
			cgltf_material& gltf_material = gltf_data->materials[i];
			
			// GLTF 2.0 Spec states that:
			// - Base color textures are encoded in SRGB
			// - Normal textures are encoded in linear
			// - Metallic roughness textures are encoded in linear
			materials[i].albedo_factor = *(glm::vec4*)&gltf_material.pbr_metallic_roughness.base_color_factor;
			if (gltf_material.pbr_metallic_roughness.base_color_texture.texture)
			{
				size_t texture_handle_index = CGLTFGetIndex<cgltf_image>(gltf_data->images,
					gltf_material.pbr_metallic_roughness.base_color_texture.texture->image);
				materials[i].albedo_texture_handle = LoadGLTFTexture(gltf_data->images[texture_handle_index], filepath, TextureFormat_RGBA8_SRGB);
			}

			if (gltf_material.normal_texture.texture)
			{
				size_t texture_handle_index = CGLTFGetIndex<cgltf_image>(gltf_data->images,
					gltf_material.normal_texture.texture->image);
				materials[i].normal_texture_handle = LoadGLTFTexture(gltf_data->images[texture_handle_index], filepath, TextureFormat_RGBA8_UNORM);
			}

			materials[i].metallic_factor = gltf_material.pbr_metallic_roughness.metallic_factor;
			materials[i].roughness_factor = gltf_material.pbr_metallic_roughness.roughness_factor;
			if (gltf_material.pbr_metallic_roughness.metallic_roughness_texture.texture)
			{
				size_t texture_handle_index = CGLTFGetIndex<cgltf_image>(gltf_data->images,
					gltf_material.pbr_metallic_roughness.metallic_roughness_texture.texture->image);
				materials[i].metallic_roughness_texture_handle = LoadGLTFTexture(gltf_data->images[texture_handle_index], filepath, TextureFormat_RGBA8_UNORM);
			}

			if (gltf_material.has_clearcoat)
			{
				materials[i].has_clearcoat = gltf_material.has_clearcoat;
				materials[i].clearcoat_alpha_factor = gltf_material.clearcoat.clearcoat_factor;
				materials[i].clearcoat_roughness_factor = gltf_material.clearcoat.clearcoat_roughness_factor;

				if (gltf_material.clearcoat.clearcoat_texture.texture)
				{
					size_t texture_handle_index = CGLTFGetIndex<cgltf_image>(gltf_data->images, gltf_material.clearcoat.clearcoat_texture.texture->image);
					materials[i].clearcoat_alpha_texture_handle = LoadGLTFTexture(gltf_data->images[texture_handle_index], filepath, TextureFormat_RGBA8_UNORM);
				}

				if (gltf_material.clearcoat.clearcoat_normal_texture.texture)
				{
					size_t texture_handle_index = CGLTFGetIndex<cgltf_image>(gltf_data->images, gltf_material.clearcoat.clearcoat_normal_texture.texture->image);
					materials[i].clearcoat_normal_texture_handle = LoadGLTFTexture(gltf_data->images[texture_handle_index], filepath, TextureFormat_RGBA8_UNORM);
				}

				if (gltf_material.clearcoat.clearcoat_roughness_texture.texture)
				{
					size_t texture_handle_index = CGLTFGetIndex<cgltf_image>(gltf_data->images, gltf_material.clearcoat.clearcoat_roughness_texture.texture->image);
					materials[i].clearcoat_roughness_texture_handle = LoadGLTFTexture(gltf_data->images[texture_handle_index], filepath, TextureFormat_RGBA8_UNORM);
				}
			}
		}

		// Create all nodes
		model.nodes.resize(gltf_data->nodes_count);

		for (size_t i = 0; i < gltf_data->nodes_count; ++i)
		{
			cgltf_node& gltf_node = gltf_data->nodes[i];
			Model::Node& model_node = model.nodes[i];
			model_node.transform = CGLTFNodeGetTransform(gltf_node);

			if (gltf_node.name)
				model_node.name = gltf_node.name;
			else
				model_node.name = filepath + std::to_string(i);

			model_node.children.resize(gltf_node.children_count);
			for (size_t j = 0; j < gltf_node.children_count; ++j)
			{
				model_node.children[j] = CGLTFGetIndex(gltf_data->nodes, gltf_node.children[j]);
			}

			if (gltf_node.mesh)
			{
				model_node.mesh_handles.resize(gltf_node.mesh->primitives_count);
				model_node.materials.resize(gltf_node.mesh->primitives_count);

				for (size_t j = 0; j < gltf_node.mesh->primitives_count; ++j)
				{
					cgltf_primitive& primitive = gltf_node.mesh->primitives[j];

					size_t mesh_handle_index = CGLTFGetIndex<cgltf_mesh>(gltf_data->meshes, gltf_node.mesh) +
						CGLTFGetIndex<cgltf_primitive>(gltf_node.mesh->primitives, &primitive);
					model_node.mesh_handles[j] = mesh_handles[mesh_handle_index];

					size_t material_handle_index = CGLTFGetIndex<cgltf_material>(gltf_data->materials, primitive.material);
					model_node.materials[j] = materials[material_handle_index];
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

	void LoadTexture(const std::string& filepath, const std::string& name, TextureFormat format, bool gen_mips, bool is_environment_map)
	{
		ReadImageResult image = ReadImage(filepath, IsHDRFormat(format));

		Renderer::CreateTextureArgs args = {};
		args.width = (uint32_t)image.width;
		args.height = (uint32_t)image.height;
		args.src_stride = (uint32_t)(image.num_components * image.component_size);
		args.format = format;
		args.pixels = std::vector<uint8_t>(image.pixels, image.pixels + (image.width * image.height * args.src_stride));
		args.generate_mips = gen_mips;
		args.is_environment_map = is_environment_map;

		TextureHandle_t texture_handle = Renderer::CreateTexture(args);
		data.textures.emplace(name, texture_handle);

		stbi_image_free(image.pixels);
	}

	TextureHandle_t GetTexture(const std::string& name)
	{
		auto iter = data.textures.find(name);
		if (iter != data.textures.end())
		{
			return iter->second;
		}

		return TextureHandle_t();
	}

	void LoadGLTF(const std::string& filepath, const std::string& name)
	{
		Model model = ReadGLTF(filepath);
		data.models.emplace(name, model);
	}

	Model* GetModel(const std::string& name)
	{
		auto iter = data.models.find(name);
		if (iter != data.models.end())
		{
			return &data.models.at(name);
		}

		return nullptr;
	}

}
