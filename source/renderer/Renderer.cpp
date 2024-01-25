#include "Precomp.h"
#include "renderer/Renderer.h"
#include "renderer/VulkanBackend.h"
#include "renderer/VulkanResourceTracker.h"
#include "renderer/ResourceSlotmap.h"
#include "renderer/RenderPass.h"
#include "renderer/Texture.h"
#include "renderer/Buffer.h"
#include "renderer/Sampler.h"
#include "Shared.glsl.h"
#include "Assets.h"

#include "vulkan/vulkan.h"
#include "vulkan/vk_enum_string_helper.h"

#include "GLFW/glfw3.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_vulkan.h"

namespace Renderer
{

#define BEGIN_PASS(cmd_buffer, pass, begin_info) pass.Begin(cmd_buffer, begin_info)
#define END_PASS(cmd_buffer, pass) pass.End(cmd_buffer)

	static constexpr uint32_t MAX_DRAW_LIST_ENTRIES = 10000;
	static constexpr uint32_t IBL_HDR_CUBEMAP_RESOLUTION = 1024;
	static constexpr uint32_t IBL_IRRADIANCE_CUBEMAP_RESOLUTION = 64;
	static constexpr uint32_t IBL_IRRADIANCE_CUBEMAP_SAMPLE_MULTIPLIER = 4;
	static constexpr uint32_t IBL_PREFILTERED_CUBEMAP_RESOLUTION = 1024;
	static constexpr uint32_t IBL_PREFILTERED_CUBEMAP_NUM_SAMPLES = 32;
	static constexpr uint32_t IBL_BRDF_LUT_RESOLUTION = 1024;
	static constexpr uint32_t IBL_BRDF_LUT_SAMPLES = 1024;

	static constexpr std::array<glm::vec3, 8> UNIT_CUBE_VERTICES =
	{
		glm::vec3(1.0, -1.0, -1.0),
		glm::vec3(1.0, -1.0, 1.0),
		glm::vec3(-1.0, -1.0, 1.0),
		glm::vec3(-1.0, -1.0, -1.0),
		glm::vec3(1.0, 1.0, -1.0),
		glm::vec3(1.0, 1.0, 1.0),
		glm::vec3(-1.0, 1.0, 1.0),
		glm::vec3(-1.0, 1.0, -1.0)
	};

	static constexpr std::array<uint16_t, 36> UNIT_CUBE_INDICES =
	{
		0, 1, 3, 3, 1, 2,
		1, 5, 2, 2, 5, 6,
		5, 4, 6, 6, 4, 7,
		4, 0, 7, 7, 0, 3,
		3, 2, 7, 7, 2, 6,
		4, 5, 0, 0, 5, 1
	};

	static const std::array<glm::mat4, 6> CUBE_FACING_VIEW_MATRICES =
	{
		// POSITIVE_X
		glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
		// NEGATIVE_X
		glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
		// POSITIVE_Y
		glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
		// NEGATIVE_Y
		glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
		// POSITIVE_Z
		glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
		// NEGATIVE_Z
		glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
	};

	struct DrawList
	{
		struct Entry
		{
			uint32_t index;

			MeshHandle_t mesh_handle;
			MaterialData material_data;
			glm::mat4 transform;
		};

		uint32_t next_free_entry = 0;
		std::array<Entry, MAX_DRAW_LIST_ENTRIES> entries;

		Entry& GetNextEntry()
		{
			VK_ASSERT(next_free_entry < MAX_DRAW_LIST_ENTRIES &&
				"Exceeded the maximum amount of draw list entries");

			Entry& entry = entries[next_free_entry];
			entry.index = next_free_entry;
			next_free_entry++;
			return entry;
		}

		void Reset()
		{
			next_free_entry = 0;
		}
	};

	struct TextureResource
	{
		std::unique_ptr<Texture> texture = nullptr;
		std::shared_ptr<Sampler> sampler = nullptr;

		VkDescriptorSet imgui_descriptor_set = VK_NULL_HANDLE;

		TextureResource() = default;
		TextureResource(std::unique_ptr<Texture>&& _texture, std::shared_ptr<Sampler> _sampler)
			: texture(std::move(_texture)), sampler(_sampler)
		{
			imgui_descriptor_set = ImGui_ImplVulkan_AddTexture(sampler->GetVkSampler(), texture->GetView()->view, texture->GetView()->GetLayout());
		}
	};

	struct MeshResource
	{
		std::unique_ptr<Buffer> vertex_buffer = nullptr;
		std::unique_ptr<Buffer> index_buffer = nullptr;

		MeshResource() = default;
		MeshResource(std::unique_ptr<Buffer>&& _vertex_buffer, std::unique_ptr<Buffer>&& _index_buffer)
			: vertex_buffer(std::move(_vertex_buffer)), index_buffer(std::move(_index_buffer)) {}
	};

	struct Data
	{
		::GLFWwindow* window = nullptr;

		// Resource slotmaps
		ResourceSlotmap<TextureResource> texture_slotmap;
		ResourceSlotmap<MeshResource> mesh_slotmap;

		uint32_t descriptor_buffer_indices[5] = { 0, 1, 2, 3, 4 };
		VkDeviceSize descriptor_buffer_offsets[5] = { 0, 0, 0, 0, 0 };

		// Render passes
		struct RenderPasses
		{
			// Frame render passes
			RenderPass skybox{ RENDER_PASS_TYPE_GRAPHICS };
			RenderPass lighting{ RENDER_PASS_TYPE_GRAPHICS };
			RenderPass post_process{ RENDER_PASS_TYPE_COMPUTE };
			
			// Resource processing render passes
			RenderPass gen_cubemap{ RENDER_PASS_TYPE_GRAPHICS };
			RenderPass gen_irradiance_cube{ RENDER_PASS_TYPE_GRAPHICS };
			RenderPass gen_prefiltered_cube{ RENDER_PASS_TYPE_GRAPHICS };
			RenderPass gen_brdf_lut{ RENDER_PASS_TYPE_GRAPHICS };
		} render_passes;

		struct RenderTargets
		{
			std::unique_ptr<Texture> hdr;
			std::unique_ptr<Texture> depth;
			std::unique_ptr<Texture> sdr;
		} render_targets;

		struct IBL
		{
			TextureHandle_t brdf_lut_handle;
		} ibl;

		struct Sync
		{
			uint64_t semaphore_value = 0;
			VkSemaphore in_flight_semaphore_timeline = VK_NULL_HANDLE;
		} sync;

		struct Frame
		{
			VkCommandBuffer command_buffer = VK_NULL_HANDLE;

			struct Sync
			{
				VkSemaphore render_finished_semaphore_binary = VK_NULL_HANDLE;
				uint64_t render_finished_value = 0;
			} sync;

			struct UBOs
			{
				std::unique_ptr<Buffer> camera_ubo;
				std::unique_ptr<Buffer> light_ubo;
				std::unique_ptr<Buffer> material_ubo;
				std::unique_ptr<Buffer> settings_ubo;
			} ubos;

			std::unique_ptr<Buffer> instance_buffer;
		} per_frame[VulkanInstance::MAX_FRAMES_IN_FLIGHT];

		// Draw submission list
		DrawList draw_list;
		uint32_t num_pointlights;

		// Default resources
		TextureHandle_t default_white_texture_handle;
		TextureHandle_t default_normal_texture_handle;
		TextureHandle_t white_furnace_skybox_handle;

		std::shared_ptr<Sampler> default_sampler;
		std::shared_ptr<Sampler> hdr_equirect_sampler;
		std::shared_ptr<Sampler> hdr_cube_sampler;
		std::shared_ptr<Sampler> irradiance_cube_sampler;
		std::shared_ptr<Sampler> prefiltered_cube_sampler;
		std::shared_ptr<Sampler> brdf_lut_sampler;

		std::unique_ptr<Buffer> unit_cube_vb;
		std::unique_ptr<Buffer> unit_cube_ib;

		TextureHandle_t skybox_texture_handle;
		RenderSettings settings;

		struct Statistics
		{
			uint32_t total_vertex_count = 0;
			uint32_t total_triangle_count = 0;

			void Reset()
			{
				total_vertex_count = 0;
				total_triangle_count = 0;
			}
		} stats;

		struct ImGui
		{
			VkDescriptorPool descriptor_pool;
			RenderPass render_pass{ RENDER_PASS_TYPE_GRAPHICS };
		} imgui;
	} static *data;

	static inline Data::Frame* GetFrameCurrent()
	{
		return &data->per_frame[vk_inst.current_frame];
	}
	
	static std::vector<VkVertexInputBindingDescription> GetVertexBindingDescription()
	{
		std::vector<VkVertexInputBindingDescription> binding_descs(2);
		binding_descs[0].binding = 0;
		binding_descs[0].stride = sizeof(Vertex);
		binding_descs[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		binding_descs[1].binding = 1;
		binding_descs[1].stride = sizeof(glm::mat4);
		binding_descs[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

		return binding_descs;
	}

	static std::vector<VkVertexInputAttributeDescription> GetVertexAttributeDescription()
	{
		std::vector<VkVertexInputAttributeDescription> attribute_desc(8);
		attribute_desc[0].binding = 0;
		attribute_desc[0].location = 0;
		attribute_desc[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		attribute_desc[0].offset = offsetof(Vertex, pos);

		attribute_desc[1].binding = 0;
		attribute_desc[1].location = 1;
		attribute_desc[1].format = VK_FORMAT_R32G32_SFLOAT;
		attribute_desc[1].offset = offsetof(Vertex, tex_coord);

		attribute_desc[2].binding = 0;
		attribute_desc[2].location = 2;
		attribute_desc[2].format = VK_FORMAT_R32G32B32_SFLOAT;
		attribute_desc[2].offset = offsetof(Vertex, normal);

		attribute_desc[3].binding = 0;
		attribute_desc[3].location = 3;
		attribute_desc[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attribute_desc[3].offset = offsetof(Vertex, tangent);

		attribute_desc[4].binding = 1;
		attribute_desc[4].location = 4;
		attribute_desc[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attribute_desc[4].offset = 0;

		attribute_desc[5].binding = 1;
		attribute_desc[5].location = 5;
		attribute_desc[5].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attribute_desc[5].offset = 16;

		attribute_desc[6].binding = 1;
		attribute_desc[6].location = 6;
		attribute_desc[6].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attribute_desc[6].offset = 32;

		attribute_desc[7].binding = 1;
		attribute_desc[7].location = 7;
		attribute_desc[7].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attribute_desc[7].offset = 48;

		return attribute_desc;
	}

	static void CreateCommandBuffers()
	{
		VkCommandBufferAllocateInfo alloc_info = {};
		alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		alloc_info.commandPool = vk_inst.cmd_pools.graphics;
		alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		alloc_info.commandBufferCount = 1;

		for (uint32_t i = 0; i < VulkanInstance::MAX_FRAMES_IN_FLIGHT; ++i)
		{
			VkCheckResult(vkAllocateCommandBuffers(vk_inst.device, &alloc_info, &data->per_frame[i].command_buffer));
		}
	}

	static void CreateSyncObjects()
	{
		// Create timeline semaphore that keeps track of back buffers in-flight
		VkSemaphoreTypeCreateInfo timeline_create_info = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
		timeline_create_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
		timeline_create_info.initialValue = 0;

		VkSemaphoreCreateInfo semaphore_info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		semaphore_info.flags = 0;
		semaphore_info.pNext = &timeline_create_info;

		VkCheckResult(vkCreateSemaphore(vk_inst.device, &semaphore_info, nullptr, &data->sync.in_flight_semaphore_timeline));

		// Create binary semaphore for each frame in-flight for the swapchain to wait on
		VkSemaphoreTypeCreateInfo binary_create_info = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
		binary_create_info.semaphoreType = VK_SEMAPHORE_TYPE_BINARY;

		semaphore_info.pNext = &binary_create_info;

		for (size_t i = 0; i < VulkanInstance::MAX_FRAMES_IN_FLIGHT; ++i)
		{
			VkCheckResult(vkCreateSemaphore(vk_inst.device, &semaphore_info, nullptr, &data->per_frame[i].sync.render_finished_semaphore_binary));
		}
	}
	
	static void CreateDefaultSamplers()
	{
		// Create default sampler
		SamplerCreateInfo sampler_info = {};

		sampler_info.address_u = SAMPLER_ADDRESS_MODE_REPEAT;
		sampler_info.address_v = SAMPLER_ADDRESS_MODE_REPEAT;
		sampler_info.address_w = SAMPLER_ADDRESS_MODE_REPEAT;
		sampler_info.border_color = SAMPLER_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
		
		sampler_info.filter_min = SAMPLER_FILTER_LINEAR; // Undersampling
		sampler_info.filter_mag = SAMPLER_FILTER_LINEAR; // Oversampling
		sampler_info.filter_mip = SAMPLER_FILTER_LINEAR; // Mip

		sampler_info.enable_anisotropy = VK_TRUE;

		sampler_info.min_lod = 0.0f;
		sampler_info.max_lod = std::numeric_limits<float>::max();
		sampler_info.name = "Default Sampler";

		data->default_sampler = Sampler::Create(sampler_info);
		
		// Create IBL samplers
		sampler_info.address_u = SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler_info.address_v = SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler_info.address_w = SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler_info.border_color = SAMPLER_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		//sampler_info.enable_anisotropy = VK_FALSE;
		sampler_info.name = "HDR Equirectangular Sampler";
		data->hdr_equirect_sampler = Sampler::Create(sampler_info);

		sampler_info.name = "Irradiance Cubemap Sampler";
		data->irradiance_cube_sampler = Sampler::Create(sampler_info);

		sampler_info.name = "BRDF LUT Sampler";
		data->brdf_lut_sampler = Sampler::Create(sampler_info);

		sampler_info.name = "HDR Cubemap Sampler";
		data->hdr_cube_sampler = Sampler::Create(sampler_info);

		sampler_info.name = "Prefiltered Cubemap Sampler";
		data->prefiltered_cube_sampler = Sampler::Create(sampler_info);
	}

	static void CreateDefaultTextures()
	{
		// Default white texture
		std::vector<uint8_t> white_pixel = { 0xFF, 0xFF, 0xFF, 0xFF };
		
		CreateTextureArgs texture_args = {};
		texture_args.format = TEXTURE_FORMAT_RGBA8_UNORM;
		texture_args.width = 1;
		texture_args.height = 1;
		texture_args.src_stride = 4;
		texture_args.pixels = white_pixel;

		data->default_white_texture_handle = CreateTexture(texture_args);

		texture_args.generate_mips = true;
		texture_args.is_environment_map = true;

		data->white_furnace_skybox_handle = CreateTexture(texture_args);

		// Default normal texture
		std::vector<uint8_t> normal_pixel = { 127, 127, 255, 255 };
		texture_args.pixels = normal_pixel;
		texture_args.generate_mips = false;
		texture_args.is_environment_map = false;

		data->default_normal_texture_handle = CreateTexture(texture_args);
	}

	static void CreateUnitCubeBuffers()
	{
		// Calculate the vertex and index buffer size
		VkDeviceSize vb_size = UNIT_CUBE_VERTICES.size() * sizeof(glm::vec3);
		VkDeviceSize ib_size = UNIT_CUBE_INDICES.size() * sizeof(uint16_t);

		// Create the staging buffer
		std::unique_ptr<Buffer> staging_buffer = Buffer::CreateStaging(vb_size + ib_size, "Unit Cube VB IB staging");

		// Write data to the staging buffer
		staging_buffer->Write(vb_size, (void*)&UNIT_CUBE_VERTICES[0], 0, 0);
		staging_buffer->Write(ib_size, (void*)&UNIT_CUBE_INDICES[0], 0, vb_size);

		// Create cube vertex and index buffer
		data->unit_cube_vb = Buffer::CreateVertex(vb_size, "Unit Cube VB");
		data->unit_cube_ib = Buffer::CreateIndex(ib_size, "Unit Cube IB");

		// Copy staged vertex and index data to the device local buffers
		data->unit_cube_vb->CopyFromImmediate(*staging_buffer, vb_size, 0, 0);
		data->unit_cube_ib->CopyFromImmediate(*staging_buffer, ib_size, vb_size, 0);
	}

	static void CreateUniformBuffers()
	{
		VkDeviceSize settings_buffer_size = sizeof(RenderSettings);
		VkDeviceSize camera_buffer_size = sizeof(CameraData);
		VkDeviceSize light_buffer_size = sizeof(uint32_t) + MAX_LIGHT_SOURCES * sizeof(PointlightData);
		VkDeviceSize material_buffer_size = MAX_UNIQUE_MATERIALS * sizeof(MaterialData);

		for (size_t i = 0; i < VulkanInstance::MAX_FRAMES_IN_FLIGHT; ++i)
		{
			// NOTE: We need to do this for UBO descriptors for now... because a descriptor buffer offsets need to be aligned to a specific boundary
			// And since you need one UBO descriptor for each frame (or you could also update it each frame), this is what we need to do.
			data->per_frame[i].ubos.settings_ubo = Buffer::CreateUniform(settings_buffer_size, "Settings UBO");
			data->per_frame[i].ubos.settings_ubo->WriteDescriptorInfo(vk_inst.device_props.descriptor_buffer_offset_alignment);

			data->per_frame[i].ubos.camera_ubo = Buffer::CreateUniform(camera_buffer_size, "Camera UBO");
			data->per_frame[i].ubos.camera_ubo->WriteDescriptorInfo();

			data->per_frame[i].ubos.light_ubo = Buffer::CreateUniform(light_buffer_size, "Light UBO");
			data->per_frame[i].ubos.light_ubo->WriteDescriptorInfo();

			data->per_frame[i].ubos.material_ubo = Buffer::CreateUniform(material_buffer_size, "Material UBO");
			data->per_frame[i].ubos.material_ubo->WriteDescriptorInfo();
		}
	}

	static void CreateInstanceBuffers()
	{
		VkDeviceSize instance_buffer_size = MAX_DRAW_LIST_ENTRIES * sizeof(glm::mat4);

		for (size_t i = 0; i < VulkanInstance::MAX_FRAMES_IN_FLIGHT; ++i)
		{
			data->per_frame[i].instance_buffer = Buffer::CreateInstance(instance_buffer_size, "Instance Buffer");
		}
	}

	static void CreateRenderTargets()
	{
		// Create HDR render target
		{
			// Remove old HDR render target
			data->render_targets.hdr.reset();

			// Create HDR render target
			TextureCreateInfo texture_info = {
				.format = TEXTURE_FORMAT_RGBA16_SFLOAT,
				.usage_flags = TEXTURE_USAGE_READ_ONLY | TEXTURE_USAGE_RENDER_TARGET,
				.dimension = TEXTURE_DIMENSION_2D,
				.width = vk_inst.swapchain.extent.width,
				.height = vk_inst.swapchain.extent.height,
				.num_mips = 1,
				.num_layers = 1,
				.name = "HDR Render Target"
			};
			data->render_targets.hdr = Texture::Create(texture_info);

			TextureView* hdr_view = data->render_targets.hdr->GetView();
			hdr_view->WriteDescriptorInfo(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

			data->render_passes.skybox.SetAttachment(RenderPass::ATTACHMENT_SLOT_COLOR0, hdr_view);
			data->render_passes.lighting.SetAttachment(RenderPass::ATTACHMENT_SLOT_COLOR0, hdr_view);
			data->render_passes.post_process.SetAttachment(RenderPass::ATTACHMENT_SLOT_READ_ONLY0, hdr_view);
		}

		// Create depth render target
		{
			// Remove old depth render target
			data->render_targets.depth.reset();

			// Create SDR render target
			TextureCreateInfo texture_info = {
				.format = TEXTURE_FORMAT_D32_SFLOAT,
				.usage_flags = TEXTURE_USAGE_DEPTH_TARGET,
				.dimension = TEXTURE_DIMENSION_2D,
				.width = vk_inst.swapchain.extent.width,
				.height = vk_inst.swapchain.extent.height,
				.num_mips = 1,
				.num_layers = 1,
				.name = "Depth Render Target"
			};
			data->render_targets.depth = Texture::Create(texture_info);

			TextureView* depth_view = data->render_targets.depth->GetView();
			data->render_passes.skybox.SetAttachment(RenderPass::ATTACHMENT_SLOT_DEPTH_STENCIL, depth_view);
			data->render_passes.lighting.SetAttachment(RenderPass::ATTACHMENT_SLOT_DEPTH_STENCIL, depth_view);
		}

		// Create SDR render target
		{
			// Remove old sdr render target
			data->render_targets.sdr.reset();

			TextureCreateInfo texture_info = {
				.format = TEXTURE_FORMAT_RGBA8_UNORM,
				.usage_flags = TEXTURE_USAGE_READ_WRITE | TEXTURE_USAGE_RENDER_TARGET | TEXTURE_USAGE_COPY_SRC | TEXTURE_USAGE_COPY_DST,
				.dimension = TEXTURE_DIMENSION_2D,
				.width = vk_inst.swapchain.extent.width,
				.height = vk_inst.swapchain.extent.height,
				.num_mips = 1,
				.num_layers = 1,
				.name = "SDR Render Target"
			};
			data->render_targets.sdr = Texture::Create(texture_info);

			TextureView* sdr_view = data->render_targets.sdr->GetView();
			sdr_view->WriteDescriptorInfo(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

			data->render_passes.post_process.SetAttachment(RenderPass::ATTACHMENT_SLOT_READ_WRITE0, sdr_view);
			data->imgui.render_pass.SetAttachment(RenderPass::ATTACHMENT_SLOT_COLOR0, sdr_view);
		}
	}

	static void CreateRenderPasses()
	{
		const auto& descriptor_buffer_layouts = Vulkan::GetDescriptorBufferLayouts();

		// Skybox raster pass
		{
			std::vector<RenderPass::AttachmentInfo> attachment_infos(2);
			attachment_infos[0].slot = RenderPass::ATTACHMENT_SLOT_COLOR0;
			attachment_infos[0].format = VK_FORMAT_R16G16B16A16_SFLOAT;
			attachment_infos[0].expected_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			attachment_infos[0].load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachment_infos[0].store_op = VK_ATTACHMENT_STORE_OP_STORE;
			attachment_infos[0].clear_value.color = { 0.0f, 0.0f, 0.0f, 1.0f };

			attachment_infos[1].slot = RenderPass::ATTACHMENT_SLOT_DEPTH_STENCIL;
			attachment_infos[1].format = VK_FORMAT_D32_SFLOAT;
			attachment_infos[1].expected_layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
			attachment_infos[1].load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachment_infos[1].store_op = VK_ATTACHMENT_STORE_OP_STORE;
			attachment_infos[1].clear_value.depthStencil = { 1.0f, 0 };

			data->render_passes.skybox.SetAttachmentInfos(attachment_infos);

			std::vector<VkPushConstantRange> push_ranges(1);
			push_ranges[0].size = 2 * sizeof(uint32_t);
			push_ranges[0].offset = 0;
			push_ranges[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

			Vulkan::GraphicsPipelineInfo info = {};
			info.input_bindings.resize(1);
			info.input_bindings[0].binding = 0;
			info.input_bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
			info.input_bindings[0].stride = sizeof(glm::vec3);
			info.input_attributes.resize(1);
			info.input_attributes[0].binding = 0;
			info.input_attributes[0].location = 0;
			info.input_attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
			info.input_attributes[0].offset = 0;
			info.color_attachment_formats = data->render_passes.skybox.GetColorAttachmentFormats();
			info.depth_stencil_attachment_format = data->render_passes.skybox.GetDepthStencilAttachmentFormat();
			info.depth_test = true;
			info.depth_write = false;
			info.depth_func = VK_COMPARE_OP_LESS_OR_EQUAL;
			info.cull_mode = VK_CULL_MODE_FRONT_BIT;
			info.vs_path = "assets/shaders/Skybox.vert";
			info.fs_path = "assets/shaders/Skybox.frag";

			data->render_passes.skybox.Build(descriptor_buffer_layouts, push_ranges, info);
		}

		// Lighting raster pass
		{
			std::vector<RenderPass::AttachmentInfo> attachment_infos(2);
			attachment_infos[0].slot = RenderPass::ATTACHMENT_SLOT_COLOR0;
			attachment_infos[0].format = VK_FORMAT_R16G16B16A16_SFLOAT;
			attachment_infos[0].expected_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			attachment_infos[0].load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
			attachment_infos[0].store_op = VK_ATTACHMENT_STORE_OP_STORE;

			attachment_infos[1].slot = RenderPass::ATTACHMENT_SLOT_DEPTH_STENCIL;
			attachment_infos[1].format = VK_FORMAT_D32_SFLOAT;
			attachment_infos[1].expected_layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
			attachment_infos[1].load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
			attachment_infos[1].store_op = VK_ATTACHMENT_STORE_OP_STORE;

			data->render_passes.lighting.SetAttachmentInfos(attachment_infos);

			std::vector<VkPushConstantRange> push_ranges(1);
			push_ranges[0].size = 8 * sizeof(uint32_t);
			push_ranges[0].offset = 0;
			push_ranges[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

			Vulkan::GraphicsPipelineInfo info = {};
			info.input_bindings = GetVertexBindingDescription();
			info.input_attributes = GetVertexAttributeDescription();
			info.color_attachment_formats = data->render_passes.lighting.GetColorAttachmentFormats();
			info.depth_test = true;
			info.depth_write = true;
			info.depth_func = VK_COMPARE_OP_LESS;
			info.depth_stencil_attachment_format = data->render_passes.lighting.GetDepthStencilAttachmentFormat();
			info.vs_path = "assets/shaders/PbrLighting.vert";
			info.fs_path = "assets/shaders/PbrLighting.frag";

			data->render_passes.lighting.Build(descriptor_buffer_layouts, push_ranges, info);
		}

		// Post-processing compute pass
		{
			std::vector<RenderPass::AttachmentInfo> attachment_infos(2);
			attachment_infos[0].slot = RenderPass::ATTACHMENT_SLOT_READ_ONLY0;
			attachment_infos[0].format = VK_FORMAT_R16G16B16A16_SFLOAT;
			attachment_infos[0].expected_layout = VK_IMAGE_LAYOUT_GENERAL;

			attachment_infos[1].slot = RenderPass::ATTACHMENT_SLOT_READ_WRITE0;
			attachment_infos[1].format = VK_FORMAT_R8G8B8A8_UNORM;
			attachment_infos[1].expected_layout = VK_IMAGE_LAYOUT_GENERAL;
			attachment_infos[1].load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachment_infos[1].store_op = VK_ATTACHMENT_STORE_OP_STORE;
			attachment_infos[1].clear_value.color = { 0.0f, 0.0f, 0.0f, 1.0f };

			data->render_passes.post_process.SetAttachmentInfos(attachment_infos);

			std::vector<VkPushConstantRange> push_ranges(1);
			push_ranges[0].size = 2 * sizeof(uint32_t);
			push_ranges[0].offset = 0;
			push_ranges[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

			Vulkan::ComputePipelineInfo info = {};
			info.cs_path = "assets/shaders/PostProcessCS.glsl";

			data->render_passes.post_process.Build(descriptor_buffer_layouts, push_ranges, info);
		}

		// Dear ImGui render pass
		{
			std::vector<RenderPass::AttachmentInfo> attachment_infos(1);
			attachment_infos[0].slot = RenderPass::ATTACHMENT_SLOT_COLOR0;
			attachment_infos[0].format = VK_FORMAT_R8G8B8A8_UNORM;
			attachment_infos[0].expected_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			attachment_infos[0].load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
			attachment_infos[0].store_op = VK_ATTACHMENT_STORE_OP_STORE;

			data->imgui.render_pass.SetAttachmentInfos(attachment_infos);
		}

		// Generate Cubemap from Equirectangular Map pass
		{
			std::vector<RenderPass::AttachmentInfo> attachment_infos(1);
			attachment_infos[0].slot = RenderPass::ATTACHMENT_SLOT_COLOR0;
			attachment_infos[0].format = VK_FORMAT_R16G16B16A16_SFLOAT;
			attachment_infos[0].expected_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			attachment_infos[0].load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachment_infos[0].store_op = VK_ATTACHMENT_STORE_OP_STORE;

			data->render_passes.gen_cubemap.SetAttachmentInfos(attachment_infos);

			std::vector<VkPushConstantRange> push_ranges(2);
			push_ranges[0].size = sizeof(glm::mat4);
			push_ranges[0].offset = 0;
			push_ranges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			push_ranges[1].size = 2 * sizeof(uint32_t);
			push_ranges[1].offset = push_ranges[0].size;
			push_ranges[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

			Vulkan::GraphicsPipelineInfo info = {};
			info.input_bindings.resize(1);
			info.input_bindings[0].binding = 0;
			info.input_bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
			info.input_bindings[0].stride = sizeof(glm::vec3);
			info.input_attributes.resize(1);
			info.input_attributes[0].binding = 0;
			info.input_attributes[0].location = 0;
			info.input_attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
			info.input_attributes[0].offset = 0;
			info.color_attachment_formats = data->render_passes.gen_cubemap.GetColorAttachmentFormats();
			info.vs_path = "assets/shaders/Cube.vert";
			info.fs_path = "assets/shaders/EquirectangularToCube.frag";

			data->render_passes.gen_cubemap.Build(descriptor_buffer_layouts, push_ranges, info);
		}

		// Generate Irradiance Cube pass
		{
			std::vector<RenderPass::AttachmentInfo> attachment_infos(1);
			attachment_infos[0].slot = RenderPass::ATTACHMENT_SLOT_COLOR0;
			attachment_infos[0].format = VK_FORMAT_R16G16B16A16_SFLOAT;
			attachment_infos[0].expected_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			attachment_infos[0].load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachment_infos[0].store_op = VK_ATTACHMENT_STORE_OP_STORE;
			
			data->render_passes.gen_irradiance_cube.SetAttachmentInfos(attachment_infos);
			
			std::vector<VkPushConstantRange> push_ranges(2);
			push_ranges[0].size = sizeof(glm::mat4);
			push_ranges[0].offset = 0;
			push_ranges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

			push_ranges[1].size = 2 * sizeof(uint32_t) + 2 * sizeof(float);
			push_ranges[1].offset = push_ranges[0].size;
			push_ranges[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

			Vulkan::GraphicsPipelineInfo info = {};
			info.input_bindings.resize(1);
			info.input_bindings[0].binding = 0;
			info.input_bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
			info.input_bindings[0].stride = sizeof(glm::vec3);
			info.input_attributes.resize(1);
			info.input_attributes[0].binding = 0;
			info.input_attributes[0].location = 0;
			info.input_attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
			info.input_attributes[0].offset = 0;
			info.color_attachment_formats = data->render_passes.gen_irradiance_cube.GetColorAttachmentFormats();
			info.vs_path = "assets/shaders/Cube.vert";
			info.fs_path = "assets/shaders/IrradianceCube.frag";
			
			data->render_passes.gen_irradiance_cube.Build(descriptor_buffer_layouts, push_ranges, info);
		}

		// Generate Prefiltered Cube pass
		{
			std::vector<RenderPass::AttachmentInfo> attachment_infos(1);
			attachment_infos[0].slot = RenderPass::ATTACHMENT_SLOT_COLOR0;
			attachment_infos[0].format = VK_FORMAT_R16G16B16A16_SFLOAT;
			attachment_infos[0].expected_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			attachment_infos[0].load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachment_infos[0].store_op = VK_ATTACHMENT_STORE_OP_STORE;

			data->render_passes.gen_prefiltered_cube.SetAttachmentInfos(attachment_infos);

			std::vector<VkPushConstantRange> push_ranges(2);
			push_ranges[0].size = sizeof(glm::mat4);
			push_ranges[0].offset = 0;
			push_ranges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

			push_ranges[1].size = 3 * sizeof(uint32_t) + sizeof(float);
			push_ranges[1].offset = push_ranges[0].size;
			push_ranges[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

			Vulkan::GraphicsPipelineInfo info = {};
			info.input_bindings.resize(1);
			info.input_bindings[0].binding = 0;
			info.input_bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
			info.input_bindings[0].stride = sizeof(glm::vec3);
			info.input_attributes.resize(1);
			info.input_attributes[0].binding = 0;
			info.input_attributes[0].location = 0;
			info.input_attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
			info.input_attributes[0].offset = 0;
			info.color_attachment_formats = data->render_passes.gen_prefiltered_cube.GetColorAttachmentFormats();
			info.vs_path = "assets/shaders/Cube.vert";
			info.fs_path = "assets/shaders/PrefilteredEnvCube.frag";

			data->render_passes.gen_prefiltered_cube.Build(descriptor_buffer_layouts, push_ranges, info);
		}

		// Generate BRDF LUT pass
		{
			std::vector<RenderPass::AttachmentInfo> attachment_infos(1);
			attachment_infos[0].slot = RenderPass::ATTACHMENT_SLOT_COLOR0;
			attachment_infos[0].format = VK_FORMAT_R16G16_SFLOAT;
			attachment_infos[0].expected_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			attachment_infos[0].load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachment_infos[0].store_op = VK_ATTACHMENT_STORE_OP_STORE;
			attachment_infos[0].clear_value = { 0.0, 0.0, 0.0, 1.0 };

			data->render_passes.gen_brdf_lut.SetAttachmentInfos(attachment_infos);

			std::vector<VkPushConstantRange> push_ranges(1);
			push_ranges[0].size = sizeof(uint32_t);
			push_ranges[0].offset = 0;
			push_ranges[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

			Vulkan::GraphicsPipelineInfo info = {};
			info.color_attachment_formats = data->render_passes.gen_brdf_lut.GetColorAttachmentFormats();
			info.vs_path = "assets/shaders/BRDF_LUT.vert";
			info.fs_path = "assets/shaders/BRDF_LUT.frag";
			info.cull_mode = VK_CULL_MODE_NONE;

			data->render_passes.gen_brdf_lut.Build(descriptor_buffer_layouts, push_ranges, info);
		}
	}

	static void InitDearImGui()
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGui::StyleColorsDark();

		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;

		// Create imgui descriptor pool
		// First descriptor is for the font, the other ones for descriptor sets for ImGui::Image calls
		VkDescriptorPoolSize pool_size = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 + 1000 };
		VkDescriptorPoolCreateInfo pool_info = {};
		pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		// First descriptor is for the font, the other ones for descriptor sets for ImGui::Image calls
		pool_info.maxSets = 1 + 1000;
		pool_info.poolSizeCount = 1;
		pool_info.pPoolSizes = &pool_size;
		VkCheckResult(vkCreateDescriptorPool(vk_inst.device, &pool_info, nullptr, &data->imgui.descriptor_pool));
		
		// Init imgui
		ImGui_ImplGlfw_InitForVulkan(vk_inst.window, true);
		ImGui_ImplVulkan_InitInfo init_info = {};
		init_info.Instance = vk_inst.instance;
		init_info.PhysicalDevice = vk_inst.physical_device;
		init_info.Device = vk_inst.device;
		init_info.QueueFamily = vk_inst.queue_indices.graphics_compute;
		init_info.Queue = vk_inst.queues.graphics;
		init_info.PipelineCache = VK_NULL_HANDLE;
		init_info.DescriptorPool = data->imgui.descriptor_pool;
		init_info.MinImageCount = VulkanInstance::MAX_FRAMES_IN_FLIGHT;
		init_info.ImageCount = VulkanInstance::MAX_FRAMES_IN_FLIGHT;
		init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
		init_info.Allocator = nullptr;
		init_info.UseDynamicRendering = true;
		init_info.ColorAttachmentFormat = VK_FORMAT_R8G8B8A8_UNORM;
		init_info.CheckVkResultFn = VkCheckResult;
		ImGui_ImplVulkan_Init(&init_info, nullptr);

		// Upload imgui font
		VkCommandBuffer command_buffer = Vulkan::BeginImmediateCommand();
		ImGui_ImplVulkan_CreateFontsTexture(command_buffer);
		Vulkan::EndImmediateCommand(command_buffer);
		ImGui_ImplVulkan_DestroyFontUploadObjects();
	}

	static void ExitDearImGui()
	{
		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();

		vkDestroyDescriptorPool(vk_inst.device, data->imgui.descriptor_pool, nullptr);
	}

	static std::unique_ptr<Texture> GenerateCubeMapFromEquirectangular(uint32_t src_texture_index, uint32_t src_sampler_index)
	{
		const auto& descriptor_buffer_binding_info = Vulkan::GetDescriptorBufferBindingInfos();

		// Create hdr environment cubemap
		uint32_t num_cube_mips = (uint32_t)std::floor(std::log2(std::max(IBL_HDR_CUBEMAP_RESOLUTION, IBL_HDR_CUBEMAP_RESOLUTION))) + 1;
		TextureCreateInfo texture_info = {
			.format = TEXTURE_FORMAT_RGBA16_SFLOAT,
			.usage_flags = TEXTURE_USAGE_RENDER_TARGET | TEXTURE_USAGE_SAMPLED,
			.dimension = TEXTURE_DIMENSION_CUBE,
			.width = IBL_HDR_CUBEMAP_RESOLUTION,
			.height = IBL_HDR_CUBEMAP_RESOLUTION,
			.num_mips = num_cube_mips,
			.num_layers = 6,
			.name = "HDR Environment Cubemap",
		};

		std::unique_ptr<Texture> hdr_env_cubemap = Texture::Create(texture_info);
		TextureView* cubemap_view = hdr_env_cubemap->GetView();
		cubemap_view->WriteDescriptorInfo(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		// Render all 6 faces of the cube map using 6 different camera view matrices
		VkViewport viewport = {};
		viewport.x = 0.0f, viewport.y = 0.0f;
		viewport.minDepth = 0.0f, viewport.maxDepth = 1.0f;

		VkRect2D scissor_rect = { 0, 0, IBL_HDR_CUBEMAP_RESOLUTION, IBL_HDR_CUBEMAP_RESOLUTION };

		struct PushConsts
		{
			glm::mat4 view_projection;
			uint32_t src_texture_index;
			uint32_t src_sampler_index;
		} push_consts;

		push_consts.src_texture_index = src_texture_index;
		push_consts.src_sampler_index = src_sampler_index;

		VkCommandBuffer command_buffer = Vulkan::BeginImmediateCommand();

		for (uint32_t mip = 0; mip < num_cube_mips; ++mip)
		{
			for (uint32_t face = 0; face < 6; ++face)
			{
				// Render current face to the offscreen render target
				RenderPass::BeginInfo begin_info = {};
				begin_info.render_width = static_cast<float>(IBL_HDR_CUBEMAP_RESOLUTION) * std::pow(0.5f, mip);
				begin_info.render_height = static_cast<float>(IBL_HDR_CUBEMAP_RESOLUTION) * std::pow(0.5f, mip);

				// TODO: Should we delete these temporary views after we are done generating?
				TextureView* face_view = hdr_env_cubemap->GetView(TextureViewCreateInfo{ .type = VK_IMAGE_VIEW_TYPE_2D,
					.base_mip = mip, .num_mips = 1, .base_layer = face, .num_layers = 1 });
				data->render_passes.gen_cubemap.SetAttachment(RenderPass::ATTACHMENT_SLOT_COLOR0, face_view);

				BEGIN_PASS(command_buffer, data->render_passes.gen_cubemap, begin_info);
				{
					viewport.width = begin_info.render_width;
					viewport.height = begin_info.render_height;

					scissor_rect.extent.width = viewport.width;
					scissor_rect.extent.height = viewport.height;

					vkCmdSetViewport(command_buffer, 0, 1, &viewport);
					vkCmdSetScissor(command_buffer, 0, 1, &scissor_rect);

					push_consts.view_projection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 512.0f) * CUBE_FACING_VIEW_MATRICES[face];
					data->render_passes.gen_cubemap.PushConstants(command_buffer, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &push_consts);
					data->render_passes.gen_cubemap.PushConstants(command_buffer, VK_SHADER_STAGE_FRAGMENT_BIT, 64, 2 * sizeof(uint32_t), &push_consts.src_texture_index);

					vk_inst.pFunc.cmd_bind_descriptor_buffers_ext(command_buffer, (uint32_t)descriptor_buffer_binding_info.size(), descriptor_buffer_binding_info.data());
					data->render_passes.gen_cubemap.SetDescriptorBufferOffsets(command_buffer, 0, 5, &data->descriptor_buffer_indices[0], &data->descriptor_buffer_offsets[0]);

					VkBuffer vertex_buffers = { data->unit_cube_vb->GetVkBuffer() };
					VkDeviceSize vb_offset = 0;
					vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffers, &vb_offset);
					vkCmdBindIndexBuffer(command_buffer, data->unit_cube_ib->GetVkBuffer(), 0, VK_INDEX_TYPE_UINT16);
					vkCmdDrawIndexed(command_buffer, data->unit_cube_ib->GetSize() / sizeof(uint16_t), 1, 0, 0, 0);
				}
				END_PASS(command_buffer, data->render_passes.gen_cubemap);
			}
		}

		hdr_env_cubemap->TransitionLayout(command_buffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		Vulkan::EndImmediateCommand(command_buffer);

		return hdr_env_cubemap;
	}

	static std::unique_ptr<Texture> GenerateIrradianceCube(uint32_t src_texture_index, uint32_t src_sampler_index)
	{
		const auto& descriptor_buffer_binding_info = Vulkan::GetDescriptorBufferBindingInfos();

		// Create irradiance cubemap
		uint32_t num_cube_mips = (uint32_t)std::floor(std::log2(std::max(IBL_IRRADIANCE_CUBEMAP_RESOLUTION, IBL_IRRADIANCE_CUBEMAP_RESOLUTION))) + 1;
		TextureCreateInfo texture_info = {
			.format = TEXTURE_FORMAT_RGBA16_SFLOAT,
			.usage_flags = TEXTURE_USAGE_RENDER_TARGET | TEXTURE_USAGE_SAMPLED,
			.dimension = TEXTURE_DIMENSION_CUBE,
			.width = IBL_IRRADIANCE_CUBEMAP_RESOLUTION,
			.height = IBL_IRRADIANCE_CUBEMAP_RESOLUTION,
			.num_mips = num_cube_mips,
			.num_layers = 6,
			.name = "Irradiance Cubemap",
		};

		std::unique_ptr<Texture> irradiance_cubemap = Texture::Create(texture_info);
		TextureView* cubemap_view = irradiance_cubemap->GetView();
		cubemap_view->WriteDescriptorInfo(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		// Render all 6 faces of the cube map using 6 different camera view matrices
		VkViewport viewport = {};
		viewport.x = 0.0f, viewport.y = 0.0f;
		viewport.minDepth = 0.0f, viewport.maxDepth = 1.0f;

		VkRect2D scissor_rect = { 0, 0, IBL_IRRADIANCE_CUBEMAP_RESOLUTION, IBL_IRRADIANCE_CUBEMAP_RESOLUTION };

		struct PushConsts
		{
			glm::mat4 view_projection;

			uint32_t src_texture_index;
			uint32_t src_sampler_index;
			float delta_phi = (2.0f * glm::pi<float>()) / 180.0f;
			float delta_theta = (0.5f * glm::pi<float>()) / 64.0f;
		} push_consts;

		push_consts.delta_phi /= IBL_IRRADIANCE_CUBEMAP_SAMPLE_MULTIPLIER;
		push_consts.delta_theta /= IBL_IRRADIANCE_CUBEMAP_SAMPLE_MULTIPLIER;

		push_consts.src_texture_index = src_texture_index;
		push_consts.src_sampler_index = src_sampler_index;

		VkCommandBuffer command_buffer = Vulkan::BeginImmediateCommand();

		for (uint32_t mip = 0; mip < num_cube_mips; ++mip)
		{
			for (uint32_t face = 0; face < 6; ++face)
			{
				// Render current face to the offscreen render target
				RenderPass::BeginInfo begin_info = {};
				begin_info.render_width = static_cast<float>(IBL_IRRADIANCE_CUBEMAP_RESOLUTION) * std::pow(0.5f, mip);
				begin_info.render_height = static_cast<float>(IBL_IRRADIANCE_CUBEMAP_RESOLUTION) * std::pow(0.5f, mip);

				// TODO: Should we delete these temporary views after we are done generating?
				TextureView* face_view = irradiance_cubemap->GetView(TextureViewCreateInfo{ .type = VK_IMAGE_VIEW_TYPE_2D,
					.base_mip = mip, .num_mips = 1, .base_layer = face, .num_layers = 1 });
				data->render_passes.gen_irradiance_cube.SetAttachment(RenderPass::ATTACHMENT_SLOT_COLOR0, face_view);

				BEGIN_PASS(command_buffer, data->render_passes.gen_irradiance_cube, begin_info);
				{
					viewport.width = begin_info.render_width;
					viewport.height = begin_info.render_height;

					scissor_rect.extent.width = viewport.width;
					scissor_rect.extent.height = viewport.height;

					vkCmdSetViewport(command_buffer, 0, 1, &viewport);
					vkCmdSetScissor(command_buffer, 0, 1, &scissor_rect);

					push_consts.view_projection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 512.0f) * CUBE_FACING_VIEW_MATRICES[face];
					data->render_passes.gen_irradiance_cube.PushConstants(command_buffer, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &push_consts);
					data->render_passes.gen_irradiance_cube.PushConstants(command_buffer, VK_SHADER_STAGE_FRAGMENT_BIT, 64, 2 * sizeof(uint32_t) + 2 * sizeof(float), &push_consts.src_texture_index);

					vk_inst.pFunc.cmd_bind_descriptor_buffers_ext(command_buffer, (uint32_t)descriptor_buffer_binding_info.size(), descriptor_buffer_binding_info.data());
					data->render_passes.gen_irradiance_cube.SetDescriptorBufferOffsets(command_buffer, 0, 5, &data->descriptor_buffer_indices[0], &data->descriptor_buffer_offsets[0]);

					VkBuffer vertex_buffers = { data->unit_cube_vb->GetVkBuffer() };
					VkDeviceSize vb_offset = 0;
					vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffers, &vb_offset);
					vkCmdBindIndexBuffer(command_buffer, data->unit_cube_ib->GetVkBuffer(), 0, VK_INDEX_TYPE_UINT16);
					vkCmdDrawIndexed(command_buffer, data->unit_cube_ib->GetSize() / sizeof(uint16_t), 1, 0, 0, 0);
				}
				END_PASS(command_buffer, data->render_passes.gen_irradiance_cube);
			}
		}

		irradiance_cubemap->TransitionLayout(command_buffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		Vulkan::EndImmediateCommand(command_buffer);

		return irradiance_cubemap;
	}

	static std::unique_ptr<Texture> GeneratePrefilteredEnvMap(uint32_t src_texture_index, uint32_t src_sampler_index)
	{
		const auto& descriptor_buffer_binding_info = Vulkan::GetDescriptorBufferBindingInfos();

		// Create prefiltered cubemap
		uint32_t num_cube_mips = (uint32_t)std::floor(std::log2(std::max(IBL_PREFILTERED_CUBEMAP_RESOLUTION, IBL_PREFILTERED_CUBEMAP_RESOLUTION))) + 1;
		TextureCreateInfo texture_info = {
			.format = TEXTURE_FORMAT_RGBA16_SFLOAT,
			.usage_flags = TEXTURE_USAGE_RENDER_TARGET | TEXTURE_USAGE_SAMPLED,
			.dimension = TEXTURE_DIMENSION_CUBE,
			.width = IBL_PREFILTERED_CUBEMAP_RESOLUTION,
			.height = IBL_PREFILTERED_CUBEMAP_RESOLUTION,
			.num_mips = num_cube_mips,
			.num_layers = 6,
			.name = "Prefiltered Cubemap",
		};

		std::unique_ptr<Texture> prefiltered_cubemap = Texture::Create(texture_info);
		TextureView* cubemap_view = prefiltered_cubemap->GetView();
		cubemap_view->WriteDescriptorInfo(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		VkViewport viewport = {};
		viewport.x = 0.0f, viewport.y = 0.0f;
		viewport.minDepth = 0.0f, viewport.maxDepth = 1.0f;

		VkRect2D scissor_rect = { 0, 0, IBL_PREFILTERED_CUBEMAP_RESOLUTION, IBL_PREFILTERED_CUBEMAP_RESOLUTION };

		struct PushConsts
		{
			glm::mat4 view_projection;

			uint32_t src_texture_index;
			uint32_t src_sampler_index;
			uint32_t num_samples = IBL_PREFILTERED_CUBEMAP_NUM_SAMPLES;
			float roughness;
		} push_consts;

		push_consts.src_texture_index = src_texture_index;
		push_consts.src_sampler_index = src_sampler_index;

		VkCommandBuffer command_buffer = Vulkan::BeginImmediateCommand();

		for (uint32_t mip = 0; mip < num_cube_mips; ++mip)
		{
			RenderPass::BeginInfo begin_info = {};
			begin_info.render_width = static_cast<float>(IBL_PREFILTERED_CUBEMAP_RESOLUTION) * std::pow(0.5f, mip);
			begin_info.render_height = static_cast<float>(IBL_PREFILTERED_CUBEMAP_RESOLUTION) * std::pow(0.5f, mip);

			for (uint32_t face = 0; face < 6; ++face)
			{
				// TODO: Should we delete these temporary views after we are done generating?
				TextureView* face_view = prefiltered_cubemap->GetView(TextureViewCreateInfo{ .type = VK_IMAGE_VIEW_TYPE_2D,
					.base_mip = mip, .num_mips = 1, .base_layer = face, .num_layers = 1 });
				data->render_passes.gen_prefiltered_cube.SetAttachment(RenderPass::ATTACHMENT_SLOT_COLOR0, face_view);

				BEGIN_PASS(command_buffer, data->render_passes.gen_prefiltered_cube, begin_info);
				{
					viewport.width = begin_info.render_width;
					viewport.height = begin_info.render_height;

					scissor_rect.extent.width = viewport.width;
					scissor_rect.extent.height = viewport.height;

					vkCmdSetViewport(command_buffer, 0, 1, &viewport);
					vkCmdSetScissor(command_buffer, 0, 1, &scissor_rect);

					push_consts.view_projection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 512.0f) * CUBE_FACING_VIEW_MATRICES[face];
					data->render_passes.gen_prefiltered_cube.PushConstants(command_buffer, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &push_consts);
					push_consts.roughness = (float)mip / (float)(num_cube_mips - 1);
					data->render_passes.gen_prefiltered_cube.PushConstants(command_buffer, VK_SHADER_STAGE_FRAGMENT_BIT, 64, 3 * sizeof(uint32_t) + sizeof(float), &push_consts.src_texture_index);

					vk_inst.pFunc.cmd_bind_descriptor_buffers_ext(command_buffer, (uint32_t)descriptor_buffer_binding_info.size(), descriptor_buffer_binding_info.data());
					data->render_passes.gen_prefiltered_cube.SetDescriptorBufferOffsets(command_buffer, 0, 5, &data->descriptor_buffer_indices[0], &data->descriptor_buffer_offsets[0]);

					VkBuffer vertex_buffers = { data->unit_cube_vb->GetVkBuffer() };
					VkDeviceSize vb_offset = 0;
					vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffers, &vb_offset);
					vkCmdBindIndexBuffer(command_buffer, data->unit_cube_ib->GetVkBuffer(), 0, VK_INDEX_TYPE_UINT16);
					vkCmdDrawIndexed(command_buffer, data->unit_cube_ib->GetSize() / sizeof(uint16_t), 1, 0, 0, 0);
				}
				END_PASS(command_buffer, data->render_passes.gen_prefiltered_cube);
			}
		}

		prefiltered_cubemap->TransitionLayout(command_buffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		Vulkan::EndImmediateCommand(command_buffer);

		return prefiltered_cubemap;
	}

	static void GenerateBRDF_LUT()
	{
		const auto& descriptor_buffer_binding_info = Vulkan::GetDescriptorBufferBindingInfos();

		// Create the BRDF LUT
		TextureCreateInfo texture_info = {
			.format = TEXTURE_FORMAT_RG16_SFLOAT,
			.usage_flags = TEXTURE_USAGE_RENDER_TARGET | TEXTURE_USAGE_SAMPLED,
			.dimension = TEXTURE_DIMENSION_2D,
			.width = IBL_BRDF_LUT_RESOLUTION,
			.height = IBL_BRDF_LUT_RESOLUTION,
			.num_mips = 1,
			.num_layers = 1,
			.name = "BRDF LUT",
		};

		std::unique_ptr<Texture> brdf_lut = Texture::Create(texture_info);
		TextureView* brdf_lut_view = brdf_lut->GetView();
		brdf_lut_view->WriteDescriptorInfo(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		
		VkViewport viewport = {};
		viewport.x = 0.0f, viewport.y = 0.0f;
		viewport.minDepth = 0.0f, viewport.maxDepth = 1.0f;

		VkRect2D scissor_rect = { 0, 0, IBL_BRDF_LUT_RESOLUTION, IBL_BRDF_LUT_RESOLUTION };

		struct PushConsts
		{
			uint32_t num_samples = IBL_BRDF_LUT_SAMPLES;
		} push_consts;

		VkCommandBuffer command_buffer = Vulkan::BeginImmediateCommand();

		RenderPass::BeginInfo begin_info = {};
		begin_info.render_width = IBL_BRDF_LUT_RESOLUTION;
		begin_info.render_height = IBL_BRDF_LUT_RESOLUTION;

		data->render_passes.gen_brdf_lut.SetAttachment(RenderPass::ATTACHMENT_SLOT_COLOR0, brdf_lut_view);

		BEGIN_PASS(command_buffer, data->render_passes.gen_brdf_lut, begin_info);
		{
			viewport.width = begin_info.render_width;
			viewport.height = begin_info.render_height;

			scissor_rect.extent.width = viewport.width;
			scissor_rect.extent.height = viewport.height;

			vkCmdSetViewport(command_buffer, 0, 1, &viewport);
			vkCmdSetScissor(command_buffer, 0, 1, &scissor_rect);

			data->render_passes.gen_brdf_lut.PushConstants(command_buffer, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(uint32_t), &push_consts);

			vk_inst.pFunc.cmd_bind_descriptor_buffers_ext(command_buffer, (uint32_t)descriptor_buffer_binding_info.size(), descriptor_buffer_binding_info.data());
			data->render_passes.gen_brdf_lut.SetDescriptorBufferOffsets(command_buffer, 0, 5, &data->descriptor_buffer_indices[0], &data->descriptor_buffer_offsets[0]);

			vkCmdDraw(command_buffer, 3, 1, 0, 0);
		}
		END_PASS(command_buffer, data->render_passes.gen_brdf_lut);

		brdf_lut->TransitionLayout(command_buffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		Vulkan::EndImmediateCommand(command_buffer);

		data->ibl.brdf_lut_handle = data->texture_slotmap.Insert(TextureResource(std::move(brdf_lut), data->brdf_lut_sampler));
	}

	void Init(::GLFWwindow* window)
	{
		Vulkan::Init(window);

		data = new Data();
		data->window = window;

		CreateRenderPasses();
		CreateRenderTargets();
		InitDearImGui();

		CreateCommandBuffers();
		CreateSyncObjects();

		CreateUniformBuffers();
		CreateInstanceBuffers();

		CreateUnitCubeBuffers();
		CreateDefaultSamplers();
		CreateDefaultTextures();
		GenerateBRDF_LUT();

		// Set default render settings
		data->settings.use_direct_light = true;

		data->settings.use_pbr_squared_roughness = true;
		data->settings.use_pbr_clearcoat = true;
		data->settings.pbr_diffuse_brdf_model = DIFFUSE_BRDF_MODEL_OREN_NAYAR;
		data->settings.white_furnace_test = false;

		data->settings.use_ibl = true;
		data->settings.use_ibl_specular_clearcoat = true;
		data->settings.use_ibl_specular_multiscatter = true;

		data->settings.exposure = 1.5f;
		data->settings.gamma = 2.2f;

		data->settings.debug_render_mode = DEBUG_RENDER_MODE_NONE;
	}

	void Exit()
	{
		// Wait for GPU to be idle before we start the cleanup
		VkCheckResult(vkDeviceWaitIdle(vk_inst.device));

		ExitDearImGui();

		// Start cleaning up all of the objects that won't be destroyed automatically
		vkDestroySemaphore(vk_inst.device, data->sync.in_flight_semaphore_timeline, nullptr);
		for (uint32_t i = 0; i < VulkanInstance::MAX_FRAMES_IN_FLIGHT; ++i)
		{
			vkDestroySemaphore(vk_inst.device, data->per_frame[i].sync.render_finished_semaphore_binary, nullptr);
		}

		// Clean up the renderer data
		delete data;

		// Finally, exit the vulkan render backend
		Vulkan::Exit();
	}

	void BeginFrame(const BeginFrameInfo& frame_info)
	{
		const Data::Frame* frame = GetFrameCurrent();

		// Wait for timeline semaphore to reach 
		frame = GetFrameCurrent();
		VkSemaphoreWaitInfo wait_info = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
		wait_info.semaphoreCount = 1;
		wait_info.pSemaphores = &data->sync.in_flight_semaphore_timeline;
		wait_info.pValues = &frame->sync.render_finished_value;
		wait_info.flags = 0;

		vkWaitSemaphores(vk_inst.device, &wait_info, UINT64_MAX);

		// Get the next available image from the swapchain
		VkResult result = Vulkan::SwapChainAcquireNextImage();
		if ((result == VK_ERROR_OUT_OF_DATE_KHR) || (result == VK_SUBOPTIMAL_KHR))
		{
			if (result == VK_ERROR_OUT_OF_DATE_KHR)
			{
				Vulkan::RecreateSwapChain();
				CreateRenderTargets();
			}

			return;
		}
		else
		{
			VkCheckResult(result);
		}

		// Reset and record the command buffer
		VkCommandBuffer command_buffer = frame->command_buffer;
		vkResetCommandBuffer(command_buffer, 0);

		ImGui_ImplGlfw_NewFrame();
		ImGui_ImplVulkan_NewFrame();
		ImGui::NewFrame();

		// Set UBO data for the current frame, like camera data and settings
		CameraData camera_data = {};
		camera_data.view = frame_info.view;
		camera_data.proj = frame_info.proj;
		camera_data.view_pos = glm::inverse(frame_info.view)[3];

		// Write camera and settings to their respective UBOs
		frame->ubos.camera_ubo->Write(sizeof(camera_data), &camera_data);
		frame->ubos.settings_ubo->Write(sizeof(data->settings), &data->settings);

		// If white furnace test is enabled, we want to use the white furnace environment map to render instead of the one passed in
		if (data->settings.white_furnace_test)
			data->skybox_texture_handle = data->white_furnace_skybox_handle;
		else
			data->skybox_texture_handle = frame_info.skybox_texture_handle;
	}

	void RenderFrame()
	{
		const Data::Frame* frame = GetFrameCurrent();
		const std::vector<VkDescriptorBufferBindingInfoEXT>& descriptor_buffer_binding_info = Vulkan::GetDescriptorBufferBindingInfos();
		VkCommandBuffer command_buffer = frame->command_buffer;

		VkCommandBufferBeginInfo command_buffer_begin_info = {};
		command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		VkCheckResult(vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info));

		// Update number of lights in the light ubo
		frame->ubos.light_ubo->Write(sizeof(uint32_t), &data->num_pointlights, 0, sizeof(PointlightData) * MAX_LIGHT_SOURCES);

		// TODO: Move to the vulkan backend, we should not care about descriptor buffers in the renderer
		// Update UBO descriptor buffer offset
		// We need a UBO per in-flight frame, so we need to update the offset at which we want to bind our UBOs from the descriptor buffer
		data->descriptor_buffer_offsets[DESCRIPTOR_SET_UBO] = VK_ALIGN_POW2(
			RESERVED_DESCRIPTOR_UBO_COUNT * vk_inst.current_frame * vk_inst.descriptor_sizes.uniform_buffer,
			vk_inst.device_props.descriptor_buffer_offset_alignment
		);

		// Render pass begin info
		RenderPass::BeginInfo begin_info = {};
		begin_info.render_width = vk_inst.swapchain.extent.width;
		begin_info.render_height = vk_inst.swapchain.extent.height;

		// Viewport and scissor rect
		VkViewport viewport = {};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(begin_info.render_width);
		viewport.height = static_cast<float>(begin_info.render_height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		VkRect2D scissor = {};
		scissor.offset = { 0, 0 };
		scissor.extent = vk_inst.swapchain.extent;

		// ----------------------------------------------------------------------------------------------------------------
		// Skybox pass

		BEGIN_PASS(command_buffer, data->render_passes.skybox, begin_info);
		{
			vkCmdSetViewport(command_buffer, 0, 1, &viewport);
			vkCmdSetScissor(command_buffer, 0, 1, &scissor);

			struct PushConsts
			{
				uint32_t env_texture_index;
				uint32_t env_sampler_index;
			} push_consts;

			const TextureResource* skybox_texture = data->texture_slotmap.Find(data->skybox_texture_handle);
			if (!skybox_texture)
			{
				VK_EXCEPT("Renderer::RenderFrame", "HDR environment map is missing a skybox cubemap");
			}

			push_consts.env_texture_index = skybox_texture->texture->GetView()->descriptor.GetIndex();
			push_consts.env_sampler_index = 0;

			data->render_passes.skybox.PushConstants(command_buffer, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push_consts), &push_consts);

			vk_inst.pFunc.cmd_bind_descriptor_buffers_ext(command_buffer, (uint32_t)descriptor_buffer_binding_info.size(), descriptor_buffer_binding_info.data());
			data->render_passes.skybox.SetDescriptorBufferOffsets(command_buffer, 0, 5, &data->descriptor_buffer_indices[0], &data->descriptor_buffer_offsets[0]);
			
			VkBuffer vertex_buffers = { data->unit_cube_vb->GetVkBuffer() };
			VkDeviceSize vb_offset = 0;
			vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffers, &vb_offset);
			vkCmdBindIndexBuffer(command_buffer, data->unit_cube_ib->GetVkBuffer(), 0, VK_INDEX_TYPE_UINT16);
			vkCmdDrawIndexed(command_buffer, data->unit_cube_ib->GetSize() / sizeof(uint16_t), 1, 0, 0, 0);
		}
		END_PASS(command_buffer, data->render_passes.skybox);

		// ----------------------------------------------------------------------------------------------------------------
		// Lighting pass

		BEGIN_PASS(command_buffer, data->render_passes.lighting, begin_info);
		{
			// Viewport and scissor
			vkCmdSetViewport(command_buffer, 0, 1, &viewport);
			vkCmdSetScissor(command_buffer, 0, 1, &scissor);

			const TextureResource* skybox_texture = data->texture_slotmap.Find(data->skybox_texture_handle);
			if (!skybox_texture)
			{
				VK_EXCEPT("Renderer::RenderFrame", "HDR environment map is missing a skybox cubemap");
			}

			Texture& irradiance_cubemap = skybox_texture->texture->GetChainned(0);
			Texture& prefiltered_cubemap = skybox_texture->texture->GetChainned(1);

			// Push constants
			struct PushConsts
			{
				uint32_t irradiance_cubemap_index;
				uint32_t irradiance_sampler_index;
				uint32_t prefiltered_cubemap_index;
				uint32_t prefiltered_sampler_index;
				uint32_t num_prefiltered_mips;
				uint32_t brdf_lut_index;
				uint32_t brdf_lut_sampler_index;
				uint32_t mat_index;
			} push_consts;

			push_consts.irradiance_cubemap_index = irradiance_cubemap.GetView()->descriptor.GetIndex();
			push_consts.irradiance_sampler_index = data->irradiance_cube_sampler->GetIndex();
			push_consts.prefiltered_cubemap_index = prefiltered_cubemap.GetView()->descriptor.GetIndex();
			push_consts.prefiltered_sampler_index = data->prefiltered_cube_sampler->GetIndex();
			push_consts.num_prefiltered_mips = prefiltered_cubemap.GetView()->create_info.num_mips - 1;
			push_consts.brdf_lut_index = data->texture_slotmap.Find(data->ibl.brdf_lut_handle)->texture->GetView()->descriptor.GetIndex();
			push_consts.brdf_lut_sampler_index = data->texture_slotmap.Find(data->ibl.brdf_lut_handle)->sampler->GetIndex();

			data->render_passes.lighting.PushConstants(command_buffer, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 7 * sizeof(uint32_t), &push_consts);

			// Bind descriptor buffers
			vk_inst.pFunc.cmd_bind_descriptor_buffers_ext(command_buffer, (uint32_t)descriptor_buffer_binding_info.size(), descriptor_buffer_binding_info.data());
			data->render_passes.lighting.SetDescriptorBufferOffsets(command_buffer, 0, 5, &data->descriptor_buffer_indices[0], &data->descriptor_buffer_offsets[0]);

			// Instance buffer
			const Buffer& instance_buffer = *frame->instance_buffer;

			for (uint32_t i = 0; i < data->draw_list.next_free_entry; ++i)
			{
				const DrawList::Entry& entry = data->draw_list.entries[i];

				// TODO: Default mesh
				const MeshResource* mesh = nullptr;
				if (VK_RESOURCE_HANDLE_VALID(entry.mesh_handle))
				{
					mesh = data->mesh_slotmap.Find(entry.mesh_handle);
				}
				VK_ASSERT(mesh && "Tried to render a mesh with an invalid mesh handle");

				data->render_passes.lighting.PushConstants(command_buffer, VK_SHADER_STAGE_FRAGMENT_BIT, 28, sizeof(uint32_t), &i);

				// Vertex and index buffers
				VkBuffer vertex_buffers[] = { mesh->vertex_buffer->GetVkBuffer(), instance_buffer.GetVkBuffer() };
				VkDeviceSize offsets[] = { 0, i * sizeof(glm::mat4) };
				vkCmdBindVertexBuffers(command_buffer, 0, 2, vertex_buffers, offsets);
				vkCmdBindIndexBuffer(command_buffer, mesh->index_buffer->GetVkBuffer(), 0, VK_INDEX_TYPE_UINT32);

				// Draw call
				vkCmdDrawIndexed(command_buffer, mesh->index_buffer->GetSize() / sizeof(uint32_t), 1, 0, 0, 0);

				data->stats.total_vertex_count += mesh->index_buffer->GetSize() / sizeof(Vertex);
				data->stats.total_triangle_count += (mesh->index_buffer->GetSize() / sizeof(uint32_t)) / 3;
			}
		}
		END_PASS(command_buffer, data->render_passes.lighting);

		// ----------------------------------------------------------------------------------------------------------------
		// Post-process pass

		BEGIN_PASS(command_buffer, data->render_passes.post_process, begin_info);
		{
			vk_inst.pFunc.cmd_bind_descriptor_buffers_ext(command_buffer, (uint32_t)descriptor_buffer_binding_info.size(), descriptor_buffer_binding_info.data());
			data->render_passes.post_process.SetDescriptorBufferOffsets(command_buffer, 0, 5, &data->descriptor_buffer_indices[0], &data->descriptor_buffer_offsets[0]);

			uint32_t src_dst_indices[2] = { data->render_targets.hdr->GetView()->descriptor.GetIndex(), data->render_targets.sdr->GetView()->descriptor.GetIndex() };
			data->render_passes.post_process.PushConstants(command_buffer, VK_SHADER_STAGE_COMPUTE_BIT, 0, 2 * sizeof(uint32_t), &src_dst_indices);

			uint32_t dispatch_x = VK_ALIGN_POW2(begin_info.render_width, 8) / 8;
			uint32_t dispatch_y = VK_ALIGN_POW2(begin_info.render_height, 8) / 8;
			vkCmdDispatch(command_buffer, dispatch_x, dispatch_y, 1);
		}
		END_PASS(command_buffer, data->render_passes.post_process);
	}

	void RenderUI()
	{
		ImGui::Begin("Renderer");

		ImGui::Text("Total vertex count: %u", data->stats.total_vertex_count);
		ImGui::Text("Total triangle count: %u", data->stats.total_triangle_count);

		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Settings"))
		{
			ImGui::Indent(10.0f);

			bool vsync = Vulkan::IsVSyncEnabled();
			if (ImGui::Checkbox("VSync", &vsync))
			{
				Vulkan::SetVSyncEnabled(vsync);
			}

			// ------------------------------------------------------------------------------------------------------
			// Debug settings

			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
			if (ImGui::CollapsingHeader("Debug"))
			{
				ImGui::Indent(10.0f);

				if (ImGui::BeginCombo("Debug render mode", DEBUG_RENDER_MODE_LABELS[data->settings.debug_render_mode]))
				{
					for (uint32_t i = 0; i < DEBUG_RENDER_MODE_NUM_MODES; ++i)
					{
						bool is_selected = i == data->settings.debug_render_mode;
						if (ImGui::Selectable(DEBUG_RENDER_MODE_LABELS[i], is_selected))
						{
							data->settings.debug_render_mode = i;
						}

						if (is_selected)
						{
							ImGui::SetItemDefaultFocus();
						}
					}

					ImGui::EndCombo();
				}

				ImGui::Unindent(10.0f);
			}

			// ------------------------------------------------------------------------------------------------------
			// PBR settings

			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
			if (ImGui::CollapsingHeader("PBR"))
			{
				ImGui::Indent(10.0f);

				ImGui::Checkbox("Use direct light", (bool*)&data->settings.use_direct_light);
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("If enabled, evaluates direct lighting from light sources");
				}

				ImGui::SetNextItemOpen(true, ImGuiCond_Once);
				if (ImGui::CollapsingHeader("General"))
				{
					ImGui::Indent(10.0f);

					ImGui::Checkbox("Use squared roughness", (bool*)&data->settings.use_pbr_squared_roughness);
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("If enabled, squares the roughness before doing any lighting calculations, which makes it perceptually more linear");
					}
					ImGui::Checkbox("Use clearcoat", (bool*)&data->settings.use_pbr_clearcoat);
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("Global toggle for clearcoat materials");
					}

					if (ImGui::BeginCombo("Diffuse BRDF Model", DIFFUSE_BRDF_MODEL_LABELS[data->settings.pbr_diffuse_brdf_model]))
					{
						for (uint32_t i = 0; i < DIFFUSE_BRDF_MODEL_NUM_MODELS; ++i)
						{
							bool is_selected = i == data->settings.pbr_diffuse_brdf_model;
							if (ImGui::Selectable(DIFFUSE_BRDF_MODEL_LABELS[i], is_selected))
							{
								data->settings.pbr_diffuse_brdf_model = i;
							}

							if (is_selected)
							{
								ImGui::SetItemDefaultFocus();
							}
						}

						ImGui::EndCombo();
					}
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("Select which diffuse BRDF term to use for direct diffuse lighting");
					}

					ImGui::Checkbox("White furnace test", (bool*)&data->settings.white_furnace_test);
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("If enabled, switches the HDR environment for a purely white uniformly lit environment");
					}

					ImGui::Unindent(10.0f);
				}

				ImGui::SetNextItemOpen(true, ImGuiCond_Once);
				if (ImGui::CollapsingHeader("IBL"))
				{
					ImGui::Indent(10.0f);

					ImGui::Checkbox("Use image-based lighting", (bool*)&data->settings.use_ibl);
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("Toggle image-based lighting");
					}
					ImGui::Checkbox("Use specular clearcoat", (bool*)&data->settings.use_ibl_specular_clearcoat);
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("If enabled, clearcoat materials will have their own specular lobe when evaluating specular indirect lighting");
					}
					ImGui::Checkbox("Use specular multiscatter", (bool*)&data->settings.use_ibl_specular_multiscatter);
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("If enabled, specular indirect lighting will be energy conserving, taking multiscatter specular bounces between microfacets into account");
					}
					
					ImGui::Unindent(10.0f);
				}

				ImGui::Unindent(10.0f);
			}

			// ------------------------------------------------------------------------------------------------------
			// Post-processing settings

			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
			if (ImGui::CollapsingHeader("Post-processing"))
			{
				ImGui::Indent(10.0f);

				ImGui::SliderFloat("Exposure", &data->settings.exposure, 0.001f, 20.0f, "%.2f");
				ImGui::SliderFloat("Gamma", &data->settings.gamma, 0.001f, 20.0f, "%.2f");

				ImGui::Unindent(10.0f);
			}

			ImGui::Unindent(10.0f);
		}

		ImGui::End();
	}

	void EndFrame()
	{
		Data::Frame* frame = GetFrameCurrent();
		VkCommandBuffer command_buffer = frame->command_buffer;

		// Render ImGui
		RenderPass::BeginInfo begin_info = {};
		begin_info.render_width = vk_inst.swapchain.extent.width;
		begin_info.render_height = vk_inst.swapchain.extent.height;

		BEGIN_PASS(command_buffer, data->imgui.render_pass, begin_info);
		{
			ImGui::Render();
			ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command_buffer, nullptr);

			ImGui::EndFrame();
		}
		END_PASS(command_buffer, data->imgui.render_pass);

		// Transition SDR render target and swapchain image to TRANSFER_SRC and TRANSFER_DST
		data->render_targets.sdr->TransitionLayout(command_buffer, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

		VkImageMemoryBarrier2 copy_barrier = VulkanResourceTracker::ImageMemoryBarrier(
			vk_inst.swapchain.images[vk_inst.swapchain.current_image], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		Vulkan::CmdImageMemoryBarrier(command_buffer, 1, &copy_barrier);

		// Copy the contents of the SDR render target into the currently active swapchain back buffer
		Vulkan::CopyToSwapchain(command_buffer, data->render_targets.sdr->GetVkImage());

		// Transition the active swapchain back buffer to PRESENT_SRC
		VkImageMemoryBarrier2 present_barrier = VulkanResourceTracker::ImageMemoryBarrier(
			vk_inst.swapchain.images[vk_inst.swapchain.current_image], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
		Vulkan::CmdImageMemoryBarrier(command_buffer, 1, &present_barrier);

		// Submit the command buffer for execution
		VkCheckResult(vkEndCommandBuffer(command_buffer));

		std::vector<VkSemaphore> wait_semaphores = { vk_inst.swapchain.image_available_semaphores[vk_inst.current_frame] };
		VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT };

		// Update the render finished timline semaphore value for the current frame
		frame->sync.render_finished_value = ++data->sync.semaphore_value;
		const uint64_t signal_semaphore_values[] = {frame->sync.render_finished_value, 0};
		VkTimelineSemaphoreSubmitInfo timeline_semaphore_submit_info = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
		timeline_semaphore_submit_info.waitSemaphoreValueCount = 0;
		timeline_semaphore_submit_info.pWaitSemaphoreValues = nullptr;
		timeline_semaphore_submit_info.signalSemaphoreValueCount = 2;
		timeline_semaphore_submit_info.pSignalSemaphoreValues = signal_semaphore_values;
		std::vector<VkSemaphore> signal_semaphores = { data->sync.in_flight_semaphore_timeline, frame->sync.render_finished_semaphore_binary };

		VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &command_buffer;
		submit_info.waitSemaphoreCount = static_cast<uint32_t>(wait_semaphores.size());
		submit_info.pWaitSemaphores = wait_semaphores.data();
		submit_info.pWaitDstStageMask = wait_stages;
		submit_info.signalSemaphoreCount = static_cast<uint32_t>(signal_semaphores.size());
		submit_info.pSignalSemaphores = signal_semaphores.data();
		submit_info.pNext = &timeline_semaphore_submit_info;

		VkCheckResult(vkQueueSubmit(vk_inst.queues.graphics, 1, &submit_info, nullptr));

		// Reset per-frame statistics, draw list, and other data
		data->stats.Reset();
		data->draw_list.Reset();
		data->num_pointlights = 0;

		// Present
		std::vector<VkSemaphore> present_wait_semaphore = { frame->sync.render_finished_semaphore_binary };
		VkResult result = Vulkan::SwapChainPresent(present_wait_semaphore);

		if ((result == VK_ERROR_OUT_OF_DATE_KHR) || (result == VK_SUBOPTIMAL_KHR))
		{
			Vulkan::RecreateSwapChain();
			CreateRenderTargets();

			if (result == VK_ERROR_OUT_OF_DATE_KHR)
			{
				return;
			}
		}
		else
		{
			VkCheckResult(result);
		}
	}

	TextureHandle_t CreateTexture(const CreateTextureArgs& args)
	{
		// Determine the texture byte size
		VkDeviceSize image_size = args.pixels.size();

		// Create staging buffer
		std::unique_ptr<Buffer> staging_buffer = Buffer::CreateStaging(image_size, "Staging Buffer Renderer::CreateTexture");

		// Copy data into the mapped memory of the staging buffer
		staging_buffer->Write(image_size, (void*)args.pixels.data());

		// Create texture image
		uint32_t num_mips = args.generate_mips ? (uint32_t)std::floor(std::log2(std::max(args.width, args.height))) + 1 : 1;
		
		TextureCreateInfo texture_info = {
			.format = args.format,
			.usage_flags = TEXTURE_USAGE_COPY_DST | TEXTURE_USAGE_SAMPLED,
			.dimension = TEXTURE_DIMENSION_2D,
			.width = args.width,
			.height = args.height,
			.num_mips = num_mips,
			.num_layers = 1,
			.name = args.name,
		};

		// For generating mips, the texture usage also needs to be flagged as COPY_SRC (TRANSFER_SRC) for vkBlitImage
		if (num_mips > 1)
			texture_info.usage_flags |= TEXTURE_USAGE_COPY_SRC;

		std::unique_ptr<Texture> texture = Texture::Create(texture_info);
		
		// Copy staging buffer data into final texture image memory (device local)
		texture->TransitionLayoutImmediate(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		texture->CopyFromBufferImmediate(*staging_buffer, 0);

		// Generate mips
		if (num_mips > 1)
		{
			Vulkan::GenerateMips(texture->GetVkImage(), texture->GetVkFormat(), args.width, args.height, num_mips);
		}
		else
		{
			// Generate Mips will already transition the image back to READ_ONLY_OPTIMAL, if we do not generate mips, we have to do it manually
			texture->TransitionLayoutImmediate(VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
		}

		TextureView* texture_view = texture->GetView();
		texture_view->descriptor = Vulkan::AllocateDescriptors(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
		texture_view->WriteDescriptorInfo(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);

		// Generate irradiance cube map for IBL
		if (args.is_environment_map)
		{
			// Generate a cubemap from the equirectangular hdr environment map
			texture = GenerateCubeMapFromEquirectangular(texture_view->descriptor.GetIndex(), data->hdr_equirect_sampler->GetIndex());

			// Generate the irradiance cubemap from the hdr cubemap, and append it to the base environment map
			texture->AppendToChain(GenerateIrradianceCube(texture->GetView()->descriptor.GetIndex(), data->hdr_cube_sampler->GetIndex()));

			// Generate the prefiltered cubemap from the hdr cubemap, and append it to the base environment map
			texture->AppendToChain(GeneratePrefilteredEnvMap(texture->GetView()->descriptor.GetIndex(), data->hdr_cube_sampler->GetIndex()));
		}

		return data->texture_slotmap.Insert(TextureResource(std::move(texture), data->default_sampler));
	}

	void DestroyTexture(TextureHandle_t handle)
	{
		VK_ASSERT(VK_RESOURCE_HANDLE_VALID(handle) && "Tried to destroy a texture with an invalid texture handle");
		data->texture_slotmap.Delete(handle);
	}

	void ImGuiRenderTexture(TextureHandle_t handle)
	{
		VK_ASSERT(VK_RESOURCE_HANDLE_VALID(handle));

		ImVec2 size = ImVec2(
			std::min((float)vk_inst.swapchain.extent.width / 8.0f, ImGui::GetWindowSize().x),
			std::min((float)vk_inst.swapchain.extent.width / 8.0f, ImGui::GetWindowSize().y)
		);

		const TextureResource* texture_resource = data->texture_slotmap.Find(handle);
		ImGui::Image(texture_resource->imgui_descriptor_set, size);
	}

	MeshHandle_t CreateMesh(const CreateMeshArgs& args)
	{
		// Determine vertex and index buffer byte size
		VkDeviceSize vb_size = sizeof(Vertex) * args.vertices.size();
		VkDeviceSize ib_size = sizeof(uint32_t) * args.indices.size();

		// Create staging buffer
		std::unique_ptr<Buffer> staging_buffer = Buffer::CreateStaging(vb_size + ib_size, "Staging Buffer CreateMesh");

		// Write data into staging buffer
		staging_buffer->Write(vb_size, (void*)args.vertices.data(), 0, 0);
		staging_buffer->Write(ib_size, (void*)args.indices.data(), 0, vb_size);

		// Create vertex and index buffers
		std::unique_ptr<Buffer> vertex_buffer = Buffer::CreateVertex(vb_size, "Vertex Buffer " + args.name);
		std::unique_ptr<Buffer> index_buffer = Buffer::CreateIndex(ib_size, "Index Buffer " + args.name);

		// Copy staging buffer data into vertex and index buffers
		vertex_buffer->CopyFromImmediate(*staging_buffer, vb_size, 0, 0);
		index_buffer->CopyFromImmediate(*staging_buffer, ib_size, vb_size, 0);

		return data->mesh_slotmap.Insert(MeshResource(std::move(vertex_buffer), std::move(index_buffer)));
	}

	void DestroyMesh(MeshHandle_t handle)
	{
		data->mesh_slotmap.Delete(handle);
	}

	void SubmitMesh(MeshHandle_t mesh_handle, const Assets::Material& material, const glm::mat4& transform)
	{
		DrawList::Entry& entry = data->draw_list.GetNextEntry();
		entry.mesh_handle = mesh_handle;
		entry.transform = transform;

		// Write mesh transform to the instance buffer for the currently active frame
		const Data::Frame* frame = GetFrameCurrent();
		frame->instance_buffer->Write(sizeof(glm::mat4), &entry.transform, 0, sizeof(glm::mat4) * entry.index);

		// Write material data to the material UBO
		TextureResource* albedo_texture = data->texture_slotmap.Find(material.albedo_texture_handle);
		if (!albedo_texture)
			albedo_texture = data->texture_slotmap.Find(data->default_white_texture_handle);
		entry.material_data.albedo_texture_index = albedo_texture->texture->GetView()->descriptor.GetIndex();

		TextureResource* normal_texture = data->texture_slotmap.Find(material.normal_texture_handle);
		if (!normal_texture)
			normal_texture = data->texture_slotmap.Find(data->default_normal_texture_handle);
		entry.material_data.normal_texture_index = normal_texture->texture->GetView()->descriptor.GetIndex();

		TextureResource* metallic_roughness_texture = data->texture_slotmap.Find(material.metallic_roughness_texture_handle);
		if (!metallic_roughness_texture)
			metallic_roughness_texture = data->texture_slotmap.Find(data->default_white_texture_handle);
		entry.material_data.metallic_roughness_texture_index = metallic_roughness_texture->texture->GetView()->descriptor.GetIndex();

		entry.material_data.albedo_factor = material.albedo_factor;
		entry.material_data.metallic_factor = material.metallic_factor;
		entry.material_data.roughness_factor = material.roughness_factor;

		entry.material_data.has_clearcoat = material.has_clearcoat ? 1 : 0;

		TextureResource* clearcoat_alpha_texture = data->texture_slotmap.Find(material.clearcoat_alpha_texture_handle);
		if (!clearcoat_alpha_texture)
			clearcoat_alpha_texture = data->texture_slotmap.Find(data->default_white_texture_handle);
		entry.material_data.clearcoat_alpha_texture_index = clearcoat_alpha_texture->texture->GetView()->descriptor.GetIndex();

		TextureResource* clearcoat_normal_texture = data->texture_slotmap.Find(material.clearcoat_normal_texture_handle);
		if (!clearcoat_normal_texture)
			clearcoat_normal_texture = data->texture_slotmap.Find(data->default_normal_texture_handle);
		entry.material_data.clearcoat_normal_texture_index = clearcoat_normal_texture->texture->GetView()->descriptor.GetIndex();

		TextureResource* clearcoat_roughness_texture = data->texture_slotmap.Find(material.clearcoat_roughness_texture_handle);
		if (!clearcoat_roughness_texture)
			clearcoat_roughness_texture = data->texture_slotmap.Find(data->default_white_texture_handle);
		entry.material_data.clearcoat_roughness_texture_index = clearcoat_roughness_texture->texture->GetView()->descriptor.GetIndex();

		entry.material_data.clearcoat_alpha_factor = material.clearcoat_alpha_factor;
		entry.material_data.clearcoat_roughness_factor = material.clearcoat_roughness_factor;

		// Default sampler
		entry.material_data.sampler_index = data->default_sampler->GetIndex();

		// Write material data to the material ubo for the currently active frame
		frame->ubos.material_ubo->Write(sizeof(MaterialData), &entry.material_data, 0, sizeof(MaterialData) * entry.index);
	}

	void SubmitPointlight(const glm::vec3& pos, const glm::vec3& color, float intensity)
	{
		VK_ASSERT(data->num_pointlights < MAX_LIGHT_SOURCES && "Exceeded the maximum amount of light sources");

		PointlightData pointlight = {
			.pos = pos,
			.intensity = intensity,
			.color = color
		};

		const Data::Frame* frame = GetFrameCurrent();
		frame->ubos.light_ubo->Write(sizeof(PointlightData), &pointlight, 0, sizeof(PointlightData) * data->num_pointlights);

		data->num_pointlights++;
	}

}
