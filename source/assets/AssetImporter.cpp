#include "Precomp.h"
#include "assets/AssetImporter.h"
#include "assets/AssetManager.h"
#include "renderer/Renderer.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#define CGLTF_IMPLEMENTATION
#include "cgltf/cgltf.h"
#include "mikkt/mikktspace.h"

namespace AssetImporter
{

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

		void Calculate(Vertex* vertices, uint32_t num_indices, uint32_t index_stride, const uint8_t* indices)
		{
			m_user_data.vertices_ptr = vertices;
			m_user_data.num_indices = num_indices;
			m_user_data.index_stride = index_stride;
			m_user_data.indices_ptr = indices;

			m_mikkt_context.m_pUserData = &m_user_data;
			genTangSpaceDefault(&m_mikkt_context);
		}

	private:
		static int GetNumFaces(const SMikkTSpaceContext* context)
		{
			UserData* user_data = reinterpret_cast<UserData*>(context->m_pUserData);
			return user_data->num_indices / 3;
		}

		static int GetVertexIndex(const SMikkTSpaceContext* context, int iFace, int iVert)
		{
			UserData* user_data = reinterpret_cast<UserData*>(context->m_pUserData);

			uint32_t face_size = GetNumVerticesOfFace(context, iFace);
			uint32_t indices_index = (iFace * face_size) + iVert;

			if (user_data->index_stride == 4)
				return *(reinterpret_cast<const uint32_t*>(user_data->indices_ptr) + indices_index);

			return *(reinterpret_cast<const uint16_t*>(user_data->indices_ptr) + indices_index);
		}

		static int GetNumVerticesOfFace(const SMikkTSpaceContext* context, int iFace)
		{
			return 3;
		}

		static void GetPosition(const SMikkTSpaceContext* context, float outpos[], int iFace, int iVert)
		{
			UserData* user_data = reinterpret_cast<UserData*>(context->m_pUserData);

			uint32_t index = GetVertexIndex(context, iFace, iVert);
			const Vertex& vertex = user_data->vertices_ptr[index];

			outpos[0] = vertex.pos[0];
			outpos[1] = vertex.pos[1];
			outpos[2] = vertex.pos[2];
		}

		static void GetNormal(const SMikkTSpaceContext* context, float outnormal[], int iFace, int iVert)
		{
			UserData* user_data = reinterpret_cast<UserData*>(context->m_pUserData);

			uint32_t index = GetVertexIndex(context, iFace, iVert);
			const Vertex& vertex = user_data->vertices_ptr[index];

			outnormal[0] = vertex.normal[0];
			outnormal[1] = vertex.normal[1];
			outnormal[2] = vertex.normal[2];
		}

		static void GetTexCoord(const SMikkTSpaceContext* context, float outuv[], int iFace, int iVert)
		{
			UserData* user_data = reinterpret_cast<UserData*>(context->m_pUserData);

			uint32_t index = GetVertexIndex(context, iFace, iVert);
			const Vertex& vertex = user_data->vertices_ptr[index];

			outuv[0] = vertex.tex_coord[0];
			outuv[1] = vertex.tex_coord[1];
		}

		static void SetTSpaceBasic(const SMikkTSpaceContext* context, const float tangentu[], float fSign, int iFace, int iVert)
		{
			UserData* user_data = reinterpret_cast<UserData*>(context->m_pUserData);

			uint32_t index = GetVertexIndex(context, iFace, iVert);
			Vertex& vertex = user_data->vertices_ptr[index];

			vertex.tangent[0] = tangentu[0];
			vertex.tangent[1] = tangentu[1];
			vertex.tangent[2] = tangentu[2];
			vertex.tangent[3] = fSign;
		}

	private:
		SMikkTSpaceInterface m_mikkt_interface = {};
		SMikkTSpaceContext m_mikkt_context = {};

		struct UserData
		{
			Vertex* vertices_ptr = nullptr;

			uint32_t num_indices = 0;
			uint32_t index_stride = 0;
			const uint8_t* indices_ptr = nullptr;
		};

		UserData m_user_data = {};

	} static s_tangent_calculator;

	struct ReadImageResult
	{
		int32_t width;
		int32_t height;
		int32_t num_components;
		int32_t component_size;

		uint8_t* pixels;
	};

	static ReadImageResult ReadImage(const std::filesystem::path& filepath)
	{
		ReadImageResult result = {};

		bool hdr = stbi_is_hdr(filepath.string().c_str());
		stbi_set_flip_vertically_on_load(hdr);
		stbi_info(filepath.string().c_str(), &result.width, &result.height, &result.num_components);

		if (hdr)
		{
			result.pixels = (uint8_t*)stbi_loadf(filepath.string().c_str(), &result.width, &result.height, &result.num_components, STBI_rgb_alpha);
			result.num_components = 4;
			result.component_size = 4;
		}
		else
		{
			result.pixels = (uint8_t*)stbi_load(filepath.string().c_str(), &result.width, &result.height, &result.num_components, STBI_rgb_alpha);
			result.num_components = 4;
			result.component_size = 1;
		}

		return result;
	}

	static uint32_t GetGLTFMeshCount(cgltf_data* gltf_data)
	{
		uint32_t num_meshes = 0;

		for (uint32_t i = 0; i < gltf_data->meshes_count; ++i)
		{
			cgltf_mesh* mesh = &gltf_data->meshes[i];
			num_meshes += mesh->primitives_count;
		}

		return num_meshes;
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
	static uint32_t CGLTFGetIndex(const T* arr, const T* elem)
	{
		return static_cast<uint32_t>(elem - arr);
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

	static TextureAsset* LoadGLTFTexture(const std::filesystem::path& filepath, cgltf_image* gltf_image, TextureFormat format)
	{
		std::filesystem::path image_filepath = filepath;
		image_filepath /= gltf_image->uri;

		AssetManager::ImportTexture(image_filepath, format, true, false);
		TextureAsset* texture_asset = AssetManager::GetAsset<TextureAsset>(MakeAssetHandleFromFilepath(image_filepath));
		// This would load it twice since GetAsset will load it if not loaded
		//LoadTexture(*texture_asset);

		return texture_asset;
	}

	static std::vector<MaterialAsset> LoadGLTFMaterials(const std::filesystem::path& filepath, cgltf_data* gltf_data)
	{
		// GLTF 2.0 Spec states that:
		// - Base color textures are encoded in SRGB
		// - Normal textures are encoded in linear
		// - Metallic roughness textures are encoded in linear

		std::vector<MaterialAsset> material_assets(gltf_data->materials_count);

		for (uint32_t i = 0; i < gltf_data->materials_count; ++i)
		{
			cgltf_material& gltf_material = gltf_data->materials[i];
			MaterialAsset& material_asset = material_assets[i];
			material_asset.type = ASSET_TYPE_MATERIAL;
			material_asset.handle = gltf_material.name ?
				MakeAssetHandleFromFilepath(filepath.string() + gltf_material.name) :
				MakeAssetHandleFromFilepath(filepath.string() + "_material" + std::to_string(i));
			material_asset.filepath = filepath;
			material_asset.load_state = ASSET_LOAD_STATE_LOADED;

			material_asset.albedo_factor = *(glm::vec4*)&gltf_material.pbr_metallic_roughness.base_color_factor;
			material_asset.metallic_factor = gltf_material.pbr_metallic_roughness.metallic_factor;
			material_asset.roughness_factor = gltf_material.pbr_metallic_roughness.roughness_factor;

			if (gltf_material.pbr_metallic_roughness.base_color_texture.texture)
			{
				material_asset.tex_albedo_render_handle = LoadGLTFTexture(filepath.parent_path(),
					gltf_material.pbr_metallic_roughness.base_color_texture.texture->image, TEXTURE_FORMAT_RGBA8_SRGB)->texture_render_handle;
			}

			if (gltf_material.normal_texture.texture)
			{
				material_asset.tex_normal_render_handle = LoadGLTFTexture(filepath.parent_path(),
					gltf_material.normal_texture.texture->image, TEXTURE_FORMAT_RGBA8_UNORM)->texture_render_handle;
			}

			if (gltf_material.pbr_metallic_roughness.metallic_roughness_texture.texture)
			{
				material_asset.tex_metal_rough_render_handle = LoadGLTFTexture(filepath.parent_path(),
					gltf_material.pbr_metallic_roughness.metallic_roughness_texture.texture->image, TEXTURE_FORMAT_RGBA8_UNORM)->texture_render_handle;
			}

			if (gltf_material.has_clearcoat)
			{
				material_asset.has_clearcoat = gltf_material.has_clearcoat;
				material_asset.clearcoat_alpha_factor = gltf_material.clearcoat.clearcoat_factor;
				material_asset.clearcoat_roughness_factor = gltf_material.clearcoat.clearcoat_roughness_factor;

				if (gltf_material.clearcoat.clearcoat_texture.texture)
				{
					material_asset.tex_cc_alpha_render_handle = LoadGLTFTexture(filepath.parent_path(),
						gltf_material.clearcoat.clearcoat_texture.texture->image, TEXTURE_FORMAT_RGBA8_UNORM)->texture_render_handle;
				}

				if (gltf_material.clearcoat.clearcoat_normal_texture.texture)
				{
					material_asset.tex_cc_normal_render_handle = LoadGLTFTexture(filepath.parent_path(),
						gltf_material.clearcoat.clearcoat_normal_texture.texture->image, TEXTURE_FORMAT_RGBA8_UNORM)->texture_render_handle;
				}

				if (gltf_material.clearcoat.clearcoat_roughness_texture.texture)
				{
					material_asset.tex_cc_rough_render_handle = LoadGLTFTexture(filepath.parent_path(),
						gltf_material.clearcoat.clearcoat_roughness_texture.texture->image, TEXTURE_FORMAT_RGBA8_UNORM)->texture_render_handle;
				}
			}
		}

		return material_assets;
	}

	static std::vector<RenderResourceHandle> LoadGLTFMeshes(const std::filesystem::path& filepath, cgltf_data* gltf_data)
	{
		// Determine how many meshes we have in total
		uint32_t num_meshes = GetGLTFMeshCount(gltf_data);
		uint32_t mesh_handle_index_current = 0;
		std::vector<RenderResourceHandle> mesh_render_handles(num_meshes);

		for (uint32_t i = 0; i < gltf_data->meshes_count; ++i)
		{
			cgltf_mesh& gltf_mesh = gltf_data->meshes[i];

			for (uint32_t j = 0; j < gltf_mesh.primitives_count; ++j)
			{
				cgltf_primitive& gltf_prim = gltf_mesh.primitives[j];

				// Prepare mesh creation arguments for renderer upload
				Renderer::CreateMeshArgs mesh_args = {};

				// Load all indices as bytes
				mesh_args.num_indices = static_cast<uint32_t>(gltf_prim.indices->count);
				mesh_args.index_stride = static_cast<uint32_t>(gltf_prim.indices->stride);

				uint32_t total_bytes_indices = mesh_args.num_indices * mesh_args.index_stride;
				mesh_args.indices_bytes = std::span<uint8_t>(CGLTFGetDataPointer<uint8_t>(gltf_prim.indices), total_bytes_indices);

				// Load vertices for current primitive
				std::vector<Vertex> vertices(gltf_prim.attributes[0].data->count);
				bool calc_tangents = true;

				for (uint32_t k = 0; k < gltf_prim.attributes_count; ++k)
				{
					cgltf_attribute& attribute = gltf_prim.attributes[k];

					switch (attribute.type)
					{
					case cgltf_attribute_type_position:
					{
						glm::vec3* pos_ptr = CGLTFGetDataPointer<glm::vec3>(attribute.data);

						for (uint32_t l = 0; l < attribute.data->count; ++l)
						{
							vertices[l].pos[0] = pos_ptr[l].x;
							vertices[l].pos[1] = pos_ptr[l].y;
							vertices[l].pos[2] = pos_ptr[l].z;
						}
						break;
					}
					case cgltf_attribute_type_texcoord:
					{
						glm::vec2* texcoord_ptr = CGLTFGetDataPointer<glm::vec2>(attribute.data);

						for (uint32_t l = 0; l < attribute.data->count; ++l)
						{
							vertices[l].tex_coord[0] = texcoord_ptr[l].x;
							vertices[l].tex_coord[1] = texcoord_ptr[l].y;
						}
						break;
					}
					case cgltf_attribute_type_normal:
					{
						glm::vec3* normal_ptr = CGLTFGetDataPointer<glm::vec3>(attribute.data);

						for (uint32_t l = 0; l < attribute.data->count; ++l)
						{
							vertices[l].normal[0] = normal_ptr[l].x;
							vertices[l].normal[1] = normal_ptr[l].y;
							vertices[l].normal[2] = normal_ptr[l].z;
						}
						break;
					}
					case cgltf_attribute_type_tangent:
					{
						glm::vec4* tangent_ptr = CGLTFGetDataPointer<glm::vec4>(attribute.data);

						for (uint32_t l = 0; l < attribute.data->count; ++l)
						{
							vertices[l].tangent[0] = tangent_ptr[l].x;
							vertices[l].tangent[1] = tangent_ptr[l].y;
							vertices[l].tangent[2] = tangent_ptr[l].z;
							vertices[l].tangent[3] = tangent_ptr[l].w;
						}
						calc_tangents = false;
						break;
					}
					}
				}

				// No tangents found, so we need to calculate them ourselves
				// Bitangents will be made in the shaders to reduce memory bandwidth
				if (calc_tangents)
					s_tangent_calculator.Calculate(vertices.data(), mesh_args.num_indices, mesh_args.index_stride, mesh_args.indices_bytes.data());

				// Set the mesh arguments for the vertices
				mesh_args.num_vertices = static_cast<uint32_t>(vertices.size());
				mesh_args.vertex_stride = sizeof(vertices[0]);

				uint32_t total_bytes_vertices = mesh_args.num_vertices * mesh_args.vertex_stride;
				mesh_args.vertices_bytes = std::span<uint8_t>(reinterpret_cast<uint8_t*>(vertices.data()), total_bytes_vertices);

				mesh_render_handles[mesh_handle_index_current++] = Renderer::CreateMesh(mesh_args);
			}
		}
		
		return mesh_render_handles;
	}

	static std::vector<uint32_t> ParseGLTFRootNodeIndices(cgltf_data* gltf_data)
	{
		std::vector<uint32_t> root_node_indices;

		for (uint32_t i = 0; i < gltf_data->nodes_count; ++i)
		{
			cgltf_node& gltf_node = gltf_data->nodes[i];
			if (!gltf_node.parent)
			{
				root_node_indices.push_back(i);
			}
		}

		return root_node_indices;
	}

	static std::vector<ModelAsset::Node> ParseGLTFNodes(const std::filesystem::path& filepath, cgltf_data* gltf_data,
		const std::vector<MaterialAsset>& material_assets, const std::vector<RenderResourceHandle>& mesh_render_handles)
	{
		// Create all nodes
		std::vector<ModelAsset::Node> model_nodes(gltf_data->nodes_count);

		for (uint32_t i = 0; i < gltf_data->nodes_count; ++i)
		{
			cgltf_node& gltf_node = gltf_data->nodes[i];
			ModelAsset::Node& model_node = model_nodes[i];

			model_node.transform = CGLTFNodeGetTransform(gltf_node);
			model_node.children.resize(gltf_node.children_count);

			for (uint32_t j = 0; j < gltf_node.children_count; ++j)
				model_node.children[j] = CGLTFGetIndex(gltf_data->nodes, gltf_node.children[j]);

			if (gltf_node.mesh)
			{
				model_node.mesh_names.resize(gltf_node.mesh->primitives_count);
				model_node.mesh_render_handles.resize(gltf_node.mesh->primitives_count);
				model_node.materials.resize(gltf_node.mesh->primitives_count);

				for (uint32_t j = 0; j < gltf_node.mesh->primitives_count; ++j)
				{
					cgltf_primitive& gltf_prim = gltf_node.mesh->primitives[j];

					if (gltf_node.name)
						model_node.mesh_names[j] = gltf_node.name + std::to_string(j);
					else
						model_node.mesh_names[j] = filepath.string() + std::to_string(j);

					uint32_t mesh_handle_index = CGLTFGetIndex<cgltf_mesh>(gltf_data->meshes, gltf_node.mesh) +
						CGLTFGetIndex<cgltf_primitive>(gltf_node.mesh->primitives, &gltf_prim);
					model_node.mesh_render_handles[j] = mesh_render_handles[mesh_handle_index];

					uint32_t material_handle_index = CGLTFGetIndex<cgltf_material>(gltf_data->materials, gltf_prim.material);
					model_node.materials[j] = material_assets[material_handle_index];
				}
			}
		}

		return model_nodes;
	}

	struct ReadGLTFResult
	{
		cgltf_data* data;
	};

	static ReadGLTFResult ReadGLTFModel(const std::filesystem::path& filepath)
	{
		cgltf_options options = {};
		/*options.memory.alloc_func = [](void* user, cgltf_size size)
		{
		};
		options.memory.free_func = [](void* user, void* ptr)
		{
		};*/

		cgltf_data* gltf_data = nullptr;
		cgltf_result parsed = cgltf_parse_file(&options, filepath.string().c_str(), &gltf_data);

		if (parsed != cgltf_result_success)
			VK_EXCEPT("AssetImporter", "Failed to load GLTF file: {}", filepath.string());

		cgltf_load_buffers(&options, gltf_data, filepath.string().c_str());

		ReadGLTFResult result = {};
		result.data = gltf_data;

		return result;
	}

	AssetHandle MakeAssetHandleFromFilepath(const std::filesystem::path& filepath)
	{
		return std::hash<std::filesystem::path>{}(filepath);
	}

	AssetType GetAssetTypeFromFileExtension(const std::filesystem::path& filepath)
	{
		if (!filepath.has_extension())
			return ASSET_TYPE_NUM_TYPES;

		if (filepath.extension() == ".png" ||
			filepath.extension() == ".jpg" ||
			filepath.extension() == ".jpeg" ||
			filepath.extension() == ".hdr")
			return ASSET_TYPE_TEXTURE;

		if (filepath.extension() == ".gltf")
			return ASSET_TYPE_MODEL;

		return ASSET_TYPE_NUM_TYPES;
	}

	std::unique_ptr<TextureAsset> ImportTexture(const std::filesystem::path& filepath, TextureFormat format, bool gen_mips, bool is_environment_map)
	{
		if (!filepath.has_extension())
			return nullptr;

		if (filepath.extension() == ".png" ||
			filepath.extension() == ".jpg" ||
			filepath.extension() == ".jpeg" ||
			filepath.extension() == ".hdr")
		{
			TextureAsset texture_asset = {};
			texture_asset.type = ASSET_TYPE_TEXTURE;
			texture_asset.handle = MakeAssetHandleFromFilepath(filepath);
			texture_asset.load_state = ASSET_LOAD_STATE_IMPORTED;
			texture_asset.filepath = filepath;

			texture_asset.format = format;
			texture_asset.mips = gen_mips;
			texture_asset.is_environment_map = is_environment_map;

			return std::make_unique<TextureAsset>(texture_asset);
		}

		return nullptr;
	}

	void LoadTexture(TextureAsset& texture_asset)
	{
		ReadImageResult image = ReadImage(texture_asset.filepath);

		Renderer::CreateTextureArgs texture_args = {};
		texture_args.width = (uint32_t)image.width;
		texture_args.height = (uint32_t)image.height;
		texture_args.src_stride = (uint32_t)(image.num_components * image.component_size);
		texture_args.format = texture_asset.format;
		uint32_t total_image_byte_size = texture_args.width * texture_args.height * texture_args.src_stride;
		texture_args.pixel_bytes = std::span<uint8_t>(image.pixels, total_image_byte_size);
		texture_args.generate_mips = texture_asset.mips;
		texture_args.is_environment_map = texture_asset.is_environment_map;

		texture_asset.load_state = ASSET_LOAD_STATE_LOADED;
		texture_asset.texture_render_handle = Renderer::CreateTexture(texture_args);
		texture_asset.preview_texture_render_handle = texture_asset.texture_render_handle;

		stbi_image_free(image.pixels);
	}

	std::unique_ptr<ModelAsset> ImportModel(const std::filesystem::path& filepath)
	{
		if (!filepath.has_extension())
			return nullptr;

		if (filepath.extension() == ".gltf")
		{
			ModelAsset model_asset = {};
			model_asset.type = ASSET_TYPE_MODEL;
			model_asset.handle = MakeAssetHandleFromFilepath(filepath);
			model_asset.load_state = ASSET_LOAD_STATE_IMPORTED;
			model_asset.filepath = filepath;

			return std::make_unique<ModelAsset>(model_asset);
		}
	
		return nullptr;
	}

	void LoadModel(ModelAsset& model_asset)
	{
		ReadGLTFResult gltf = ReadGLTFModel(model_asset.filepath);

		std::vector<MaterialAsset> material_assets = LoadGLTFMaterials(model_asset.filepath, gltf.data);
		std::vector<RenderResourceHandle> mesh_render_handles = LoadGLTFMeshes(model_asset.filepath, gltf.data);

		model_asset.load_state = ASSET_LOAD_STATE_LOADED;
		model_asset.root_nodes = ParseGLTFRootNodeIndices(gltf.data);
		model_asset.nodes = ParseGLTFNodes(model_asset.filepath, gltf.data, material_assets, mesh_render_handles);
		model_asset.preview_texture_render_handle = RenderResourceHandle();

		cgltf_free(gltf.data);
	}

}
