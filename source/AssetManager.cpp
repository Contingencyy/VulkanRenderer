#include "Precomp.h"
#include "AssetManager.h"
#include "renderer/Renderer.h"
#include "Shared.glsl.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf/cgltf.h"

#include "mikkt/mikktspace.h"

#include "imgui/imgui.h"

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

};

namespace AssetManager
{

	struct Data
	{
		std::filesystem::path assets_base_dir;
		std::filesystem::path models_base_dir;
		std::filesystem::path textures_base_dir;

		std::unordered_map<AssetHandle_t, std::unique_ptr<Asset>> assets;

		std::filesystem::path current_dir;
		ImVec2 asset_thumbnail_base_size = { 128.0f, 128.0f };
		ImVec2 asset_thumbnail_base_padding = { 16.0f, 16.0f };

		TangentCalculator tangent_calc;
	} static *data;

	static AssetHandle_t AssetHandleFromFilepath(const std::filesystem::path& filepath)
	{
		return AssetHandle_t(std::hash<std::filesystem::path>{}(filepath));
	}

	static bool IsAssetImported(AssetHandle_t handle)
	{
		return data->assets.find(handle) != data->assets.end();
	}

	static bool IsAssetLoaded(const Asset& asset)
	{
		return asset.load_state == ASSET_LOAD_STATE_LOADED;
	}

	static void ImportTexture(const std::filesystem::path& filepath)
	{
		if (!filepath.has_extension())
			return;

		if (filepath.extension() == ".png" ||
			filepath.extension() == ".jpg" ||
			filepath.extension() == ".jpeg" ||
			filepath.extension() == ".hdr")
		{
			AssetHandle_t handle = AssetHandleFromFilepath(filepath);

			TextureAsset texture_asset = {};
			texture_asset.handle = handle;
			texture_asset.filepath = filepath;
			texture_asset.type = ASSET_TYPE_TEXTURE;
			texture_asset.load_state = ASSET_LOAD_STATE_IMPORTED;
			texture_asset.gpu_texture_handle = TextureHandle_t();

			data->assets.insert({ handle, std::make_unique<TextureAsset>(texture_asset) });
		}
	}

	static void ImportTexturesFromDirectory(const std::filesystem::path& filepath)
	{
		for (auto& item : std::filesystem::directory_iterator(filepath))
		{
			if (item.is_directory())
			{
				ImportTexturesFromDirectory(item.path());
				continue;
			}

			if (item.is_regular_file() &&
				item.path().has_extension())
			{
				ImportTexture(item.path());
			}
		}
	}

	static void ImportModel(const std::filesystem::path& filepath)
	{
		if (!filepath.has_extension())
			return;

		if (filepath.extension() == ".gltf")
		{
			AssetHandle_t handle = {};
			handle.value = std::hash<std::filesystem::path>{}(filepath);

			if (data->assets.find(handle) != data->assets.end())
				return;

			ModelAsset model_asset = {};
			model_asset.handle = handle;
			model_asset.filepath = filepath;
			model_asset.type = ASSET_TYPE_MODEL;
			model_asset.load_state = ASSET_LOAD_STATE_IMPORTED;

			data->assets.insert({ handle, std::make_unique<ModelAsset>(model_asset) });
		}
	}

	static void ImportModelsFromDirectory(const std::filesystem::path& filepath)
	{
		for (const auto& item : std::filesystem::directory_iterator(filepath))
		{
			if (item.is_directory())
			{
				ImportModelsFromDirectory(item.path());
				continue;
			}

			if (item.is_regular_file() &&
				item.path().has_extension())
			{
				ImportModel(item.path());
			}
		}
	}

	struct ReadImageResult
	{
		int32_t width;
		int32_t height;
		int32_t num_components;
		int32_t component_size;

		uint8_t* pixels;
	};

	static ReadImageResult ReadImage(const std::filesystem::path& filepath, bool hdr)
	{
		ReadImageResult result = {};
		stbi_set_flip_vertically_on_load(hdr);

		if (hdr)
		{
			result.pixels = (uint8_t*)stbi_loadf(filepath.string().c_str(), &result.width, &result.height, &result.num_components, STBI_rgb_alpha);
			// We force stbi to load rgba values, so we always have 4 components
			result.num_components = 4;
			result.component_size = 4;
		}
		else
		{
			result.pixels = (uint8_t*)stbi_load(filepath.string().c_str(), &result.width, &result.height, &result.num_components, STBI_rgb_alpha);
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

	static TextureHandle_t LoadGLTFTexture(cgltf_image& gltf_image, const std::filesystem::path& filepath, TextureFormat format)
	{
		char* combined_char = nullptr;
		CreateFilepathFromUri(filepath.string(), gltf_image.uri, &combined_char);
		std::filesystem::path combined_filepath = combined_char;
		delete combined_char;

		AssetHandle_t handle = AssetHandleFromFilepath(combined_filepath);
		if (!IsAssetImported(handle))
			ImportTexture(combined_filepath);

		TextureAsset* texture_asset = GetAsset<TextureAsset>(handle);
		if (!IsAssetLoaded(*texture_asset))
			LoadTexture(combined_filepath, format, true, false);

		return texture_asset->gpu_texture_handle;
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

	static void ReadGLTF(const std::filesystem::path& filepath, ModelAsset& model)
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
		{
			VK_EXCEPT("AssetManager", "Failed to load GLTF file: {}", filepath.string());
		}

		cgltf_load_buffers(&options, gltf_data, filepath.string().c_str());

		// Determine how many meshes we have in total
		uint32_t num_meshes = 0;
		for (uint32_t i = 0; i < gltf_data->meshes_count; ++i)
		{
			cgltf_mesh* mesh = &gltf_data->meshes[i];
			num_meshes += mesh->primitives_count;
		}

		std::vector<MeshHandle_t> mesh_handles(num_meshes);
		uint32_t mesh_handle_index_current = 0;

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
					data->tangent_calc.Calculate(vertices.data(), mesh_args.num_indices, mesh_args.index_stride, mesh_args.indices_bytes.data());

				// Set the mesh arguments for the vertices
				mesh_args.num_vertices = static_cast<uint32_t>(vertices.size());
				mesh_args.vertex_stride = sizeof(vertices[0]);

				uint32_t total_bytes_vertices = mesh_args.num_vertices * mesh_args.vertex_stride;
				mesh_args.vertices_bytes = std::span<uint8_t>(reinterpret_cast<uint8_t*>(vertices.data()), total_bytes_vertices);

				mesh_handles[mesh_handle_index_current++] = Renderer::CreateMesh(mesh_args);
			}
		}

		// Create all materials
		std::vector<MaterialAsset> materials(gltf_data->materials_count);

		for (uint32_t i = 0; i < gltf_data->materials_count; ++i)
		{
			cgltf_material& gltf_material = gltf_data->materials[i];
			
			// GLTF 2.0 Spec states that:
			// - Base color textures are encoded in SRGB
			// - Normal textures are encoded in linear
			// - Metallic roughness textures are encoded in linear
			materials[i].albedo_factor = *(glm::vec4*)&gltf_material.pbr_metallic_roughness.base_color_factor;
			if (gltf_material.pbr_metallic_roughness.base_color_texture.texture)
			{
				uint32_t texture_handle_index = CGLTFGetIndex<cgltf_image>(gltf_data->images,
					gltf_material.pbr_metallic_roughness.base_color_texture.texture->image);
				materials[i].albedo_texture_handle = LoadGLTFTexture(gltf_data->images[texture_handle_index], filepath, TEXTURE_FORMAT_RGBA8_SRGB);
			}

			if (gltf_material.normal_texture.texture)
			{
				uint32_t texture_handle_index = CGLTFGetIndex<cgltf_image>(gltf_data->images,
					gltf_material.normal_texture.texture->image);
				materials[i].normal_texture_handle = LoadGLTFTexture(gltf_data->images[texture_handle_index], filepath, TEXTURE_FORMAT_RGBA8_UNORM);
			}

			materials[i].metallic_factor = gltf_material.pbr_metallic_roughness.metallic_factor;
			materials[i].roughness_factor = gltf_material.pbr_metallic_roughness.roughness_factor;
			if (gltf_material.pbr_metallic_roughness.metallic_roughness_texture.texture)
			{
				uint32_t texture_handle_index = CGLTFGetIndex<cgltf_image>(gltf_data->images,
					gltf_material.pbr_metallic_roughness.metallic_roughness_texture.texture->image);
				materials[i].metallic_roughness_texture_handle = LoadGLTFTexture(gltf_data->images[texture_handle_index], filepath, TEXTURE_FORMAT_RGBA8_UNORM);
			}

			if (gltf_material.has_clearcoat)
			{
				materials[i].has_clearcoat = gltf_material.has_clearcoat;
				materials[i].clearcoat_alpha_factor = gltf_material.clearcoat.clearcoat_factor;
				materials[i].clearcoat_roughness_factor = gltf_material.clearcoat.clearcoat_roughness_factor;

				if (gltf_material.clearcoat.clearcoat_texture.texture)
				{
					uint32_t texture_handle_index = CGLTFGetIndex<cgltf_image>(gltf_data->images, gltf_material.clearcoat.clearcoat_texture.texture->image);
					materials[i].clearcoat_alpha_texture_handle = LoadGLTFTexture(gltf_data->images[texture_handle_index], filepath, TEXTURE_FORMAT_RGBA8_UNORM);
				}

				if (gltf_material.clearcoat.clearcoat_normal_texture.texture)
				{
					uint32_t texture_handle_index = CGLTFGetIndex<cgltf_image>(gltf_data->images, gltf_material.clearcoat.clearcoat_normal_texture.texture->image);
					materials[i].clearcoat_normal_texture_handle = LoadGLTFTexture(gltf_data->images[texture_handle_index], filepath, TEXTURE_FORMAT_RGBA8_UNORM);
				}

				if (gltf_material.clearcoat.clearcoat_roughness_texture.texture)
				{
					uint32_t texture_handle_index = CGLTFGetIndex<cgltf_image>(gltf_data->images, gltf_material.clearcoat.clearcoat_roughness_texture.texture->image);
					materials[i].clearcoat_roughness_texture_handle = LoadGLTFTexture(gltf_data->images[texture_handle_index], filepath, TEXTURE_FORMAT_RGBA8_UNORM);
				}
			}
		}

		// Create all nodes
		model.nodes.resize(gltf_data->nodes_count);

		for (uint32_t i = 0; i < gltf_data->nodes_count; ++i)
		{
			cgltf_node& gltf_node = gltf_data->nodes[i];
			ModelAsset::Node& model_node = model.nodes[i];
			model_node.transform = CGLTFNodeGetTransform(gltf_node);

			model_node.children.resize(gltf_node.children_count);
			for (uint32_t j = 0; j < gltf_node.children_count; ++j)
			{
				model_node.children[j] = static_cast<uint32_t>(CGLTFGetIndex(gltf_data->nodes, gltf_node.children[j]));
			}

			if (gltf_node.mesh)
			{
				model_node.mesh_names.resize(gltf_node.mesh->primitives_count);
				model_node.mesh_handles.resize(gltf_node.mesh->primitives_count);
				model_node.materials.resize(gltf_node.mesh->primitives_count);

				for (uint32_t j = 0; j < gltf_node.mesh->primitives_count; ++j)
				{
					cgltf_primitive& gltf_primitive = gltf_node.mesh->primitives[j];

					if (gltf_node.name)
						model_node.mesh_names[j] = gltf_node.name + std::to_string(j);
					else
						model_node.mesh_names[j] = filepath.string() + std::to_string(j);

					uint32_t mesh_handle_index = CGLTFGetIndex<cgltf_mesh>(gltf_data->meshes, gltf_node.mesh) +
						CGLTFGetIndex<cgltf_primitive>(gltf_node.mesh->primitives, &gltf_primitive);
					model_node.mesh_handles[j] = mesh_handles[mesh_handle_index];

					uint32_t material_handle_index = CGLTFGetIndex<cgltf_material>(gltf_data->materials, gltf_primitive.material);
					model_node.materials[j] = materials[material_handle_index];
				}
			}

			if (!gltf_node.parent)
			{
				model.root_nodes.push_back(i);
			}
		}

		cgltf_free(gltf_data);
	}

	static void RenderAssetBrowserUI()
	{
		if (ImGui::BeginMenuBar())
		{
			std::filesystem::path new_path = data->current_dir;
			std::filesystem::path level_path;

			for (std::filesystem::path level : data->current_dir)
			{
				level_path /= level;

				ImGui::Text(level.string().c_str());
				if (ImGui::IsItemClicked())
				{
					new_path = level_path;
				}
				ImGui::Text("\\");
			}

			data->current_dir = new_path;

			/*if (ImGui::Button("Back"))
			{
				if (data->current_dir.has_parent_path())
					data->current_dir = data->current_dir.parent_path();
			}*/

			ImGui::EndMenuBar();
		}

		float thumbnail_width = data->asset_thumbnail_base_size.x + data->asset_thumbnail_base_padding.x;
		float content_width = ImGui::GetContentRegionAvail().x;
		int32_t num_columns = std::max(static_cast<int32_t>(content_width / thumbnail_width), 1);

		ImGui::Columns(num_columns, 0, false);

		for (const auto& item : std::filesystem::directory_iterator(data->current_dir))
		{
			std::filesystem::path relative_path = std::filesystem::relative(item.path(), data->assets_base_dir);
			std::string filename = relative_path.filename().string();

			//TextureHandle_t icon_handle = ;

			Renderer::ImGuiRenderTextureButton(TextureHandle_t(), data->asset_thumbnail_base_size.x, data->asset_thumbnail_base_size.y);
			if (ImGui::IsItemHovered() &&
				ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				if (item.is_directory())
				{
					data->current_dir /= item.path().filename();
				}
			}
			ImGui::TextWrapped(filename.c_str());

			ImGui::NextColumn();
		}

		ImGui::Columns();
	}

	void Init(const std::filesystem::path& assets_base_path)
	{
		data = new Data();

		data->assets_base_dir = assets_base_path;
		data->models_base_dir = data->assets_base_dir.string() + "\\models";
		data->textures_base_dir = assets_base_path.string() + "\\textures";

		data->current_dir = data->assets_base_dir;

		ImportTexturesFromDirectory(data->textures_base_dir);
		ImportModelsFromDirectory(data->models_base_dir);
	}

	void Exit()
	{
		delete data;
		data = nullptr;
	}

	void RenderUI()
	{
		if (ImGui::Begin("Asset Manager", nullptr, ImGuiWindowFlags_MenuBar))
		{
			RenderAssetBrowserUI();
		}
		ImGui::End();
	}

	AssetHandle_t LoadTexture(const std::filesystem::path& filepath, TextureFormat format, bool gen_mips, bool is_environment_map)
	{
		AssetHandle_t handle = AssetHandleFromFilepath(filepath);
		if (!IsAssetImported(handle))
			ImportTexture(filepath);

		TextureAsset* texture_asset = GetAsset<TextureAsset>(handle);

		// Check if the asset is loaded, and if not, load it
		if (!IsAssetLoaded(*texture_asset))
		{
			ReadImageResult image = ReadImage(filepath, IsHDRFormat(format));

			Renderer::CreateTextureArgs args = {};
			args.width = (uint32_t)image.width;
			args.height = (uint32_t)image.height;
			args.src_stride = (uint32_t)(image.num_components * image.component_size);
			args.format = format;
			uint32_t total_image_byte_size = args.width * args.height * args.src_stride;
			args.pixel_bytes = std::span<uint8_t>(image.pixels, total_image_byte_size);
			args.generate_mips = gen_mips;
			args.is_environment_map = is_environment_map;

			TextureHandle_t texture_handle = Renderer::CreateTexture(args);
			texture_asset->gpu_texture_handle = texture_handle;
			texture_asset->load_state = ASSET_LOAD_STATE_LOADED;

			stbi_image_free(image.pixels);
		}

		return handle;
	}

	AssetHandle_t LoadGLTF(const std::filesystem::path& filepath)
	{
		AssetHandle_t handle = AssetHandleFromFilepath(filepath);
		if (!IsAssetImported(handle))
			ImportModel(filepath);

		ModelAsset* model_asset = GetAsset<ModelAsset>(handle);

		if (!IsAssetLoaded(*model_asset))
		{
			ReadGLTF(filepath, *model_asset);

			model_asset->load_state = ASSET_LOAD_STATE_LOADED;
		}

		return handle;
	}

	Asset* GetAssetEx(AssetHandle_t handle)
	{
		if (IsAssetImported(handle))
			return data->assets.at(handle).get();

		return nullptr;
	}

}
