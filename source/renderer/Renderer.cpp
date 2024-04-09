#include "Precomp.h"
#include "renderer/Renderer.h"
#include "renderer/vulkan/VulkanBackend.h"
#include "renderer/vulkan/VulkanSwapChain.h"
#include "renderer/vulkan/VulkanBuffer.h"
#include "renderer/vulkan/VulkanImage.h"
#include "renderer/vulkan/VulkanImageView.h"
#include "renderer/vulkan/VulkanCommandQueue.h"
#include "renderer/vulkan/VulkanCommandPool.h"
#include "renderer/vulkan/VulkanCommandBuffer.h"
#include "renderer/vulkan/VulkanCommands.h"
#include "renderer/vulkan/VulkanSync.h"
#include "renderer/vulkan/VulkanDescriptor.h"
#include "renderer/vulkan/VulkanRaytracing.h"
#include "renderer/vulkan/VulkanUtils.h"
#include "renderer/RenderPass.h"
#include "renderer/RingBuffer.h"
#include "ResourceSlotmap.h"
#include "Shared.glsl.h"
#include "assets/AssetTypes.h"

// Used for area lights
#include "renderer/LTCMatrices.h"

#include "vulkan/vulkan.h"
#include "vulkan/vk_enum_string_helper.h"

#include "GLFW/glfw3.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_vulkan.h"

namespace Renderer
{

#define RENDER_PASS_BEGIN(render_pass) { RenderPass& current_pass = *render_pass
#define RENDER_PASS_STAGE_BEGIN(stage_index, command_buffer, render_width, render_height) VK_ASSERT(stage_index < current_pass.GetStageCount() && "Tried to begin more render pass stages than the render pass supports"); \
	current_pass.BeginStage(command_buffer, stage_index, render_width, render_height)
#define RENDER_PASS_STAGE_SET_ATTACHMENT(stage_index, attachment_slot, image_view) current_pass.SetStageAttachment(stage_index, attachment_slot, image_view)
#define RENDER_PASS_STAGE_END(stage_index, command_buffer) current_pass.EndStage(command_buffer, stage_index)
#define RENDER_PASS_END(render_pass) }

	enum RenderPassStage
	{
		RENDER_PASS_SKYBOX_STAGE_SKYBOX = 0,
		RENDER_PASS_SKYBOX_NUM_STAGES = 1,

		RENDER_PASS_GEOMETRY_STAGE_DEPTH_PREPASS = 0,
		RENDER_PASS_GEOMETRY_STAGE_LIGHTING = 1,
		RENDER_PASS_GEOMETRY_NUM_STAGES = 2,

		RENDER_PASS_POST_PROCESS_STAGE_TONEMAP_GAMMA_EXPOSURE = 0,
		RENDER_PASS_POST_PROCESS_NUM_STAGES = 1,

		RENDER_PASS_GEN_IBL_CUBEMAPS_STAGE_HDR_CUBEMAP = 0,
		RENDER_PASS_GEN_IBL_CUBEMAPS_STAGE_IRRADIANCE_CUBEMAP = 1,
		RENDER_PASS_GEN_IBL_CUBEMAPS_STAGE_PREFILTERED_CUBEMAP = 2,
		RENDER_PASS_GEN_IBL_CUBEMAPS_NUM_STAGES = 3,

		RENDER_PASS_BRDF_LUT_STAGE_BRDF_LUT = 0,
		RENDER_PASS_BRDF_LUT_NUM_STAGES = 1,

		RENDER_PASS_IMGUI_STAGE_IMGUI = 0,
		RENDER_PASS_IMGUI_NUM_STAGES = 1
	};

	static constexpr uint32_t MAX_DRAW_LIST_ENTRIES = 10000;
	static constexpr uint32_t IBL_HDR_CUBEMAP_RESOLUTION = 1024;
	static constexpr uint32_t IBL_IRRADIANCE_CUBEMAP_RESOLUTION = 64;
	static constexpr uint32_t IBL_IRRADIANCE_CUBEMAP_SAMPLE_MULTIPLIER = 4;
	static constexpr uint32_t IBL_PREFILTERED_CUBEMAP_RESOLUTION = 1024;
	static constexpr uint32_t IBL_PREFILTERED_CUBEMAP_NUM_SAMPLES = 32;
	static constexpr uint32_t IBL_BRDF_LUT_RESOLUTION = 1024;
	static constexpr uint32_t IBL_BRDF_LUT_SAMPLES = 1024;

	static constexpr std::array<Vertex, 4> UNIT_QUAD_VERTICES =
	{
		Vertex{.pos = {-0.5f, -0.5f, 0.0f}, .tex_coord = {0.0f, 1.0f}, .normal = {0.0f, 0.0f, 1.0f}, .tangent = {1.0f, 0.0f, 0.0f, 1.0f} },
		Vertex{.pos = { 0.5f, -0.5f, 0.0f}, .tex_coord = {1.0f, 1.0f}, .normal = {0.0f, 0.0f, 1.0f}, .tangent = {1.0f, 0.0f, 0.0f, 1.0f} },
		Vertex{.pos = {-0.5f,  0.5f, 0.0f}, .tex_coord = {0.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f}, .tangent = {1.0f, 0.0f, 0.0f, 1.0f} },
		Vertex{.pos = { 0.5f,  0.5f, 0.0f}, .tex_coord = {1.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f}, .tangent = {1.0f, 0.0f, 0.0f, 1.0f} }
	};

	static constexpr std::array<uint16_t, 6> UNIT_QUAD_INDICES =
	{
		0, 1, 2, 3, 2, 1
	};

	static constexpr std::array<Vertex, 24> UNIT_CUBE_VERTICES =
	{
		// FRONT
		Vertex{.pos = {-0.5f, -0.5f, 0.5f}, .tex_coord = {0.0f, 1.0f}, .normal = {0.0f, 0.0f, 1.0f}, .tangent = {1.0f, 0.0f, 0.0f, 1.0f} },
		Vertex{.pos = { 0.5f, -0.5f, 0.5f}, .tex_coord = {1.0f, 1.0f}, .normal = {0.0f, 0.0f, 1.0f}, .tangent = {1.0f, 0.0f, 0.0f, 1.0f} },
		Vertex{.pos = {-0.5f,  0.5f, 0.5f}, .tex_coord = {0.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f}, .tangent = {1.0f, 0.0f, 0.0f, 1.0f} },
		Vertex{.pos = { 0.5f,  0.5f, 0.5f}, .tex_coord = {1.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f}, .tangent = {1.0f, 0.0f, 0.0f, 1.0f} },

		// BACK
		Vertex{.pos = {-0.5f, -0.5f, -0.5f}, .tex_coord = {0.0f, 1.0f}, .normal = {0.0f, 0.0f, -1.0f}, .tangent = {-1.0f, 0.0f, 0.0f, -1.0f} },
		Vertex{.pos = { 0.5f, -0.5f, -0.5f}, .tex_coord = {1.0f, 1.0f}, .normal = {0.0f, 0.0f, -1.0f}, .tangent = {-1.0f, 0.0f, 0.0f, -1.0f} },
		Vertex{.pos = {-0.5f,  0.5f, -0.5f}, .tex_coord = {0.0f, 0.0f}, .normal = {0.0f, 0.0f, -1.0f}, .tangent = {-1.0f, 0.0f, 0.0f, -1.0f} },
		Vertex{.pos = { 0.5f,  0.5f, -0.5f}, .tex_coord = {1.0f, 0.0f}, .normal = {0.0f, 0.0f, -1.0f}, .tangent = {-1.0f, 0.0f, 0.0f, -1.0f} },

		// LEFT
		Vertex{.pos = {-0.5f, -0.5f, -0.5f}, .tex_coord = {0.0f, 1.0f}, .normal = {-1.0f, 0.0f, 0.0f}, .tangent = {0.0f, -1.0f, 0.0f, -1.0f} },
		Vertex{.pos = {-0.5f, -0.5f,  0.5f}, .tex_coord = {1.0f, 1.0f}, .normal = {-1.0f, 0.0f, 0.0f}, .tangent = {0.0f, -1.0f, 0.0f, -1.0f} },
		Vertex{.pos = {-0.5f,  0.5f, -0.5f}, .tex_coord = {0.0f, 0.0f}, .normal = {-1.0f, 0.0f, 0.0f}, .tangent = {0.0f, -1.0f, 0.0f, -1.0f} },
		Vertex{.pos = {-0.5f,  0.5f,  0.5f}, .tex_coord = {1.0f, 0.0f}, .normal = {-1.0f, 0.0f, 0.0f}, .tangent = {0.0f, -1.0f, 0.0f, -1.0f} },

		// RIGHT
		Vertex{.pos = {0.5f, -0.5f, -0.5f}, .tex_coord = {0.0f, 1.0f}, .normal = {1.0f, 0.0f, 0.0f}, .tangent = {0.0f, 1.0f, 0.0f, 1.0f} },
		Vertex{.pos = {0.5f, -0.5f,  0.5f}, .tex_coord = {1.0f, 1.0f}, .normal = {1.0f, 0.0f, 0.0f}, .tangent = {0.0f, 1.0f, 0.0f, 1.0f} },
		Vertex{.pos = {0.5f,  0.5f, -0.5f}, .tex_coord = {0.0f, 0.0f}, .normal = {1.0f, 0.0f, 0.0f}, .tangent = {0.0f, 1.0f, 0.0f, 1.0f} },
		Vertex{.pos = {0.5f,  0.5f,  0.5f}, .tex_coord = {1.0f, 0.0f}, .normal = {1.0f, 0.0f, 0.0f}, .tangent = {0.0f, 1.0f, 0.0f, 1.0f} },

		// TOP
		Vertex{.pos = {-0.5f, 0.5f, -0.5f}, .tex_coord = {0.0f, 1.0f}, .normal = {0.0f, 1.0f, 0.0f}, .tangent = {0.0f, 0.0f, 1.0f, 1.0f} },
		Vertex{.pos = {-0.5f, 0.5f,  0.5f}, .tex_coord = {1.0f, 1.0f}, .normal = {0.0f, 1.0f, 0.0f}, .tangent = {0.0f, 0.0f, 1.0f, 1.0f} },
		Vertex{.pos = { 0.5f, 0.5f, -0.5f}, .tex_coord = {0.0f, 0.0f}, .normal = {0.0f, 1.0f, 0.0f}, .tangent = {0.0f, 0.0f, 1.0f, 1.0f} },
		Vertex{.pos = { 0.5f, 0.5f,  0.5f}, .tex_coord = {1.0f, 0.0f}, .normal = {0.0f, 1.0f, 0.0f}, .tangent = {0.0f, 0.0f, 1.0f, 1.0f} },

		// BOTTOM
		Vertex{.pos = {-0.5f, -0.5f, -0.5f}, .tex_coord = {0.0f, 1.0f}, .normal = {0.0f, -1.0f, 0.0f}, .tangent = {0.0f, 0.0f, -1.0f, 1.0f} },
		Vertex{.pos = {-0.5f, -0.5f,  0.5f}, .tex_coord = {1.0f, 1.0f}, .normal = {0.0f, -1.0f, 0.0f}, .tangent = {0.0f, 0.0f, -1.0f, 1.0f} },
		Vertex{.pos = { 0.5f, -0.5f, -0.5f}, .tex_coord = {0.0f, 0.0f}, .normal = {0.0f, -1.0f, 0.0f}, .tangent = {0.0f, 0.0f, -1.0f, 1.0f} },
		Vertex{.pos = { 0.5f, -0.5f,  0.5f}, .tex_coord = {1.0f, 0.0f}, .normal = {0.0f, -1.0f, 0.0f}, .tangent = {0.0f, 0.0f, -1.0f, 1.0f} },
	};

	static constexpr std::array<uint16_t, 36> UNIT_CUBE_INDICES =
	{
		0, 1, 2, 3, 2, 1,
		4, 6, 5, 7, 5, 6,
		8, 9, 10, 11, 10, 9,
		12, 14, 13, 15, 13, 14,
		16, 17, 18, 19, 18, 17,
		20, 22, 21, 23, 21, 22
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

	struct Texture
	{
		TextureCreateInfo texture_info;

		VulkanImage image;
		VulkanImageView view;
		VulkanDescriptorAllocation view_descriptor;
		VulkanSampler sampler;

		RenderResourceHandle next;

		VkDescriptorSet imgui_descriptor_set = VK_NULL_HANDLE;
		VulkanImageView imgui_2D_view;

		Texture() = default;
		explicit Texture(const TextureCreateInfo& texture_info, VulkanImage image, VulkanImageView view, VulkanDescriptorAllocation descriptor, VulkanSampler sampler)
			: texture_info(texture_info), image(image), view(view), view_descriptor(descriptor), sampler(sampler)
		{
			TextureViewCreateInfo view_info = {};
			view_info.format = texture_info.format;
			view_info.dimension = TEXTURE_DIMENSION_2D;
			view_info.base_mip = 0;
			view_info.num_mips = texture_info.num_mips;
			view_info.base_layer = 0;
			view_info.num_layers = 1;

			imgui_2D_view = Vulkan::ImageView::Create(image, view_info);
			imgui_descriptor_set = Vulkan::AddImGuiTexture(image.vk_image, imgui_2D_view.vk_image_view, sampler.vk_sampler);
		}

		~Texture()
		{
			Vulkan::ImageView::Destroy(imgui_2D_view);

			Vulkan::Descriptor::Free(view_descriptor);
			Vulkan::ImageView::Destroy(view);
			Vulkan::Image::Destroy(image);
		}
	};

	struct VertexBuffer
	{
		VulkanBuffer buffer;
		VulkanDescriptorAllocation descriptor;
	};

	struct IndexBuffer
	{
		VulkanBuffer buffer;
		VkIndexType index_type = VK_INDEX_TYPE_MAX_ENUM;
		uint32_t num_indices = 0;
	};

	struct InstanceBuffer
	{
		RingBuffer::Allocation alloc;
		VulkanDescriptorAllocation descriptor;
	};

	struct Mesh
	{
		VertexBuffer vertex_buffer;
		IndexBuffer index_buffer;
		VulkanBuffer blas_buffer;

		Mesh() = default;
		explicit Mesh(const VertexBuffer& vertex_buffer, const IndexBuffer& index_buffer, const VulkanBuffer& blas_buffer)
			: vertex_buffer(vertex_buffer), index_buffer(index_buffer), blas_buffer(blas_buffer)
		{
		}

		~Mesh()
		{
			Vulkan::Descriptor::Free(vertex_buffer.descriptor);
			Vulkan::Buffer::Destroy(vertex_buffer.buffer);

			Vulkan::Buffer::Destroy(index_buffer.buffer);
			Vulkan::Buffer::Destroy(blas_buffer);
		}
	};

	struct RenderTarget
	{
		VulkanImage image;
		VulkanImageView view;
		VulkanDescriptorAllocation descriptor;

		~RenderTarget()
		{
			Vulkan::Descriptor::Free(descriptor);
			Vulkan::ImageView::Destroy(view);
			Vulkan::Image::Destroy(image);
		}
	};

	struct DrawList
	{
		struct Entry
		{
			uint32_t index = 0;
			
			Mesh* mesh = nullptr;
			GPUMaterial gpu_material = {};
			InstanceData instance_data = {};
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

	struct Frame
	{
		VulkanCommandBuffer command_buffer;

		struct Sync
		{
			VulkanFence render_finished_fence;
			uint64_t frame_in_flight_fence_value = 0;
		} sync;

		struct UBOs
		{
			RingBuffer::Allocation settings_ubo;
			RingBuffer::Allocation camera_ubo;
			RingBuffer::Allocation light_ubo;
			RingBuffer::Allocation material_ubo;

			VulkanDescriptorAllocation descriptors;
		} ubos;

		struct Raytracing
		{
			VulkanBuffer tlas;
			VulkanDescriptorAllocation tlas_descriptor;

			VulkanBuffer tlas_scratch;
			VulkanBuffer tlas_instance_buffer;
		} raytracing;

		InstanceBuffer instance_buffer;
	};

	struct Data
	{
		::GLFWwindow* glfw_window;

		struct Resolution
		{
			uint32_t width = 0;
			uint32_t height = 0;
		} render_resolution, output_resolution;

		struct CommandQueues
		{
			VulkanCommandQueue graphics_compute;
			VulkanCommandQueue transfer;
		} command_queues;

		struct CommandPools
		{
			VulkanCommandPool graphics_compute;
			VulkanCommandPool transfer;
		} command_pools;

		struct Camera
		{
			float near_plane = 0.1f;
			float far_plane = 10000.0f;
		} camera_settings;

		// Resource slotmaps
		ResourceSlotmap<Texture> texture_slotmap;
		ResourceSlotmap<Mesh> mesh_slotmap;

		// Ring buffer
		RingBuffer ring_buffer;

		// Render passes
		struct RenderPasses
		{
			// Frame render passes
			std::unique_ptr<RenderPass> skybox;
			std::unique_ptr<RenderPass> geometry;
			std::unique_ptr<RenderPass> post_process;
			
			// Resource processing render passes
			std::unique_ptr<RenderPass> gen_ibl_cubemaps;
			std::unique_ptr<RenderPass> gen_brdf_lut;

			// Dear ImGui render pass
			std::unique_ptr<RenderPass> imgui;
		} render_passes;

		struct RenderTargets
		{
			RenderTarget hdr;
			RenderTarget depth;
			RenderTarget sdr;
		} render_targets;

		struct IBL
		{
			RenderResourceHandle brdf_lut_handle;
		} ibl;

		Frame per_frame[Vulkan::MAX_FRAMES_IN_FLIGHT];

		// Draw submission list
		DrawList draw_list;
		uint32_t num_area_lights;

		// Default resources
		RenderResourceHandle default_white_texture_handle;
		RenderResourceHandle default_normal_texture_handle;
		
		RenderResourceHandle ltc_matrices_texture_handle;
		RenderResourceHandle ltc_ggx_fresnel_sphere_clipping_texture_handle;

		RenderResourceHandle white_furnace_skybox_handle;

		VulkanSampler default_sampler;
		VulkanSampler ibl_sampler;

		RenderResourceHandle unit_quad_mesh_handle;
		RenderResourceHandle unit_cube_mesh_handle;

		GPUMaterial default_gpu_material;

		RenderResourceHandle skybox_texture_handle;
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
	} static *data;

	static inline Frame* GetFrameCurrent()
	{
		return &data->per_frame[Vulkan::GetCurrentFrameIndex() % Vulkan::MAX_FRAMES_IN_FLIGHT];
	}

	static void CreateSyncObjects()
	{
		// Create binary semaphore for each frame in-flight for the swapchain to wait on
		for (size_t i = 0; i < Vulkan::MAX_FRAMES_IN_FLIGHT; ++i)
		{
			data->per_frame[i].sync.render_finished_fence = Vulkan::Sync::CreateFence(VULKAN_FENCE_TYPE_BINARY);
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

		data->default_sampler = Vulkan::CreateSampler(sampler_info);
		
		// Create IBL samplers
		sampler_info.address_u = SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler_info.address_v = SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler_info.address_w = SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler_info.border_color = SAMPLER_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		//sampler_info.enable_anisotropy = VK_FALSE;
		sampler_info.name = "IBL Sampler";
		data->ibl_sampler = Vulkan::CreateSampler(sampler_info);
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
		texture_args.pixel_bytes = std::span<uint8_t>(white_pixel.data(), 4);

		data->default_white_texture_handle = CreateTexture(texture_args);

		texture_args.generate_mips = true;
		texture_args.is_environment_map = true;

		data->white_furnace_skybox_handle = CreateTexture(texture_args);

		// Default normal texture
		std::vector<uint8_t> normal_pixel = { 127, 127, 255, 255 };
		texture_args.pixel_bytes = std::span<uint8_t>(normal_pixel.data(), 4);
		texture_args.generate_mips = false;
		texture_args.is_environment_map = false;

		data->default_normal_texture_handle = CreateTexture(texture_args);

		// TODO: num_samples should be defined somewhere so that the shader can also use it
		const uint32_t num_samples = 64;
		texture_args.format = TEXTURE_FORMAT_RGBA32_SFLOAT;
		texture_args.width = num_samples;
		texture_args.height = num_samples;
		texture_args.src_stride = 16;
		uint32_t total_texture_bytes = texture_args.width * texture_args.height * texture_args.src_stride;
		texture_args.pixel_bytes = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&LTC1[0]), total_texture_bytes);

		data->ltc_matrices_texture_handle = CreateTexture(texture_args);

		texture_args.pixel_bytes = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&LTC2[0]), total_texture_bytes);
		data->ltc_ggx_fresnel_sphere_clipping_texture_handle = CreateTexture(texture_args);
	}

	static void CreateDefaultMeshes()
	{
		// Create unit quad mesh
		CreateMeshArgs mesh_args = {};
		mesh_args.num_vertices = static_cast<uint32_t>(UNIT_QUAD_VERTICES.size());
		mesh_args.vertex_stride = sizeof(UNIT_QUAD_VERTICES[0]);
		uint32_t total_bytes_vertices = mesh_args.num_vertices * mesh_args.vertex_stride;
		mesh_args.vertices_bytes = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(UNIT_QUAD_VERTICES.data()), total_bytes_vertices);

		mesh_args.num_indices = static_cast<uint32_t>(UNIT_QUAD_INDICES.size());
		mesh_args.index_stride = sizeof(UNIT_QUAD_INDICES[0]);
		uint32_t total_bytes_indices = mesh_args.num_indices * mesh_args.index_stride;
		mesh_args.indices_bytes = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(UNIT_QUAD_INDICES.data()), total_bytes_indices);

		data->unit_quad_mesh_handle = CreateMesh(mesh_args);

		// Create unit cube mesh
		mesh_args.num_vertices = static_cast<uint32_t>(UNIT_CUBE_VERTICES.size());
		mesh_args.vertex_stride = sizeof(UNIT_CUBE_VERTICES[0]);
		total_bytes_vertices = mesh_args.num_vertices * mesh_args.vertex_stride;
		mesh_args.vertices_bytes = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(UNIT_CUBE_VERTICES.data()), total_bytes_vertices);

		mesh_args.num_indices = static_cast<uint32_t>(UNIT_CUBE_INDICES.size());
		mesh_args.index_stride = sizeof(UNIT_CUBE_INDICES[0]);
		total_bytes_indices = mesh_args.num_indices * mesh_args.index_stride;
		mesh_args.indices_bytes = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(UNIT_CUBE_INDICES.data()), total_bytes_indices);

		mesh_args.name = "Unit Cube";

		data->unit_cube_mesh_handle = CreateMesh(mesh_args);
	}

	static void CreateDefaultMaterial()
	{
		data->default_gpu_material.albedo_factor = glm::vec4(1.0f);
		data->default_gpu_material.albedo_texture_index = data->texture_slotmap.Find(data->default_white_texture_handle)->view_descriptor.descriptor_offset;
		data->default_gpu_material.normal_texture_index = data->texture_slotmap.Find(data->default_normal_texture_handle)->view_descriptor.descriptor_offset;
		data->default_gpu_material.metallic_factor = 1.0f;
		data->default_gpu_material.roughness_factor = 1.0f;
		data->default_gpu_material.metallic_roughness_texture_index = data->texture_slotmap.Find(data->default_white_texture_handle)->view_descriptor.descriptor_offset;

		data->default_gpu_material.has_clearcoat = false;
		data->default_gpu_material.clearcoat_alpha_factor = 1.0f;
		data->default_gpu_material.clearcoat_alpha_texture_index = data->texture_slotmap.Find(data->default_white_texture_handle)->view_descriptor.descriptor_offset;
		data->default_gpu_material.clearcoat_normal_texture_index = data->texture_slotmap.Find(data->default_normal_texture_handle)->view_descriptor.descriptor_offset;
		data->default_gpu_material.clearcoat_roughness_factor = 1.0f;
		data->default_gpu_material.clearcoat_roughness_texture_index = data->texture_slotmap.Find(data->default_white_texture_handle)->view_descriptor.descriptor_offset;

		data->default_gpu_material.sampler_index = data->default_sampler.descriptor.descriptor_offset;
		data->default_gpu_material.blackbody_radiator = false;
	}

	static void CreateRenderTargets()
	{
		// Create HDR render target
		{
			// Remove old HDR render target
			if (data->render_targets.hdr.image.vk_image)
			{
				Vulkan::Descriptor::Free(data->render_targets.hdr.descriptor);
				Vulkan::ImageView::Destroy(data->render_targets.hdr.view);
				Vulkan::Image::Destroy(data->render_targets.hdr.image);
			}

			// Create HDR render target
			TextureCreateInfo texture_info = {
				.format = TEXTURE_FORMAT_RGBA16_SFLOAT,
				.usage_flags = TEXTURE_USAGE_READ_ONLY | TEXTURE_USAGE_RENDER_TARGET,
				.dimension = TEXTURE_DIMENSION_2D,
				.width = data->output_resolution.width,
				.height = data->output_resolution.height,
				.num_mips = 1,
				.num_layers = 1,
				.name = "HDR Render Target"
			};
			data->render_targets.hdr.image = Vulkan::Image::Create(texture_info);

			TextureViewCreateInfo view_info = {
				.format = texture_info.format,
				.dimension = texture_info.dimension
			};
			data->render_targets.hdr.view = Vulkan::ImageView::Create(data->render_targets.hdr.image, view_info);
			data->render_targets.hdr.descriptor = Vulkan::Descriptor::Allocate(VULKAN_DESCRIPTOR_TYPE_STORAGE_IMAGE);
			Vulkan::Descriptor::Write(data->render_targets.hdr.descriptor, data->render_targets.hdr.view, VK_IMAGE_LAYOUT_GENERAL);
		}

		// Create depth render target
		{
			// Remove old depth render target
			if (data->render_targets.depth.image.vk_image)
			{
				Vulkan::ImageView::Destroy(data->render_targets.depth.view);
				Vulkan::Image::Destroy(data->render_targets.depth.image);
			}

			// Create SDR render target
			TextureCreateInfo texture_info = {
				.format = TEXTURE_FORMAT_D32_SFLOAT,
				.usage_flags = TEXTURE_USAGE_DEPTH_TARGET,
				.dimension = TEXTURE_DIMENSION_2D,
				.width = data->output_resolution.width,
				.height = data->output_resolution.height,
				.num_mips = 1,
				.num_layers = 1,
				.name = "Depth Render Target"
			};
			data->render_targets.depth.image = Vulkan::Image::Create(texture_info);

			TextureViewCreateInfo view_info = {
				.format = texture_info.format,
				.dimension = texture_info.dimension
			};
			data->render_targets.depth.view = Vulkan::ImageView::Create(data->render_targets.depth.image, view_info);
		}

		// Create SDR render target
		{
			// Remove old sdr render target
			if (data->render_targets.sdr.image.vk_image)
			{
				Vulkan::Descriptor::Free(data->render_targets.sdr.descriptor);
				Vulkan::ImageView::Destroy(data->render_targets.sdr.view);
				Vulkan::Image::Destroy(data->render_targets.sdr.image);
			}

			TextureCreateInfo texture_info = {
				.format = TEXTURE_FORMAT_RGBA8_UNORM,
				.usage_flags = TEXTURE_USAGE_READ_WRITE | TEXTURE_USAGE_RENDER_TARGET | TEXTURE_USAGE_COPY_SRC | TEXTURE_USAGE_COPY_DST,
				.dimension = TEXTURE_DIMENSION_2D,
				.width = data->output_resolution.width,
				.height = data->output_resolution.height,
				.num_mips = 1,
				.num_layers = 1,
				.name = "SDR Render Target"
			};
			data->render_targets.sdr.image = Vulkan::Image::Create(texture_info);

			TextureViewCreateInfo view_info = {
				.format = texture_info.format,
				.dimension = texture_info.dimension
			};
			data->render_targets.sdr.view = Vulkan::ImageView::Create(data->render_targets.sdr.image, view_info);
			data->render_targets.sdr.descriptor = Vulkan::Descriptor::Allocate(VULKAN_DESCRIPTOR_TYPE_STORAGE_IMAGE);
			Vulkan::Descriptor::Write(data->render_targets.sdr.descriptor, data->render_targets.sdr.view, VK_IMAGE_LAYOUT_GENERAL);
		}
	}

	static void CreateRenderPasses()
	{
		// Skybox pass
		{
			std::vector<RenderPass::Stage> stages(RENDER_PASS_SKYBOX_NUM_STAGES);

			// Skybox rendering stage
			Vulkan::GraphicsPipelineInfo pipeline_info = {};
			pipeline_info.color_attachment_formats = { TEXTURE_FORMAT_RGBA16_SFLOAT };
			pipeline_info.depth_stencil_attachment_format = {};
			pipeline_info.depth_test = false;
			pipeline_info.depth_write = false;
			pipeline_info.depth_func = VK_COMPARE_OP_ALWAYS;
			pipeline_info.cull_mode = VK_CULL_MODE_FRONT_BIT;
			pipeline_info.vs_path = "assets/shaders/Skybox.vert";
			pipeline_info.fs_path = "assets/shaders/Skybox.frag";

			pipeline_info.push_ranges.resize(2);
			pipeline_info.push_ranges[0].size = sizeof(uint32_t);
			pipeline_info.push_ranges[0].offset = 0;
			pipeline_info.push_ranges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

			pipeline_info.push_ranges[1].size = 2 * sizeof(uint32_t);
			pipeline_info.push_ranges[1].offset = pipeline_info.push_ranges[0].size;
			pipeline_info.push_ranges[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

			RenderPass::Stage& skybox_stage = stages[RENDER_PASS_SKYBOX_STAGE_SKYBOX];
			skybox_stage.pipeline = Vulkan::CreateGraphicsPipeline(pipeline_info);

			RenderPass::Attachment& skybox_stage_color0 = skybox_stage.attachments[RenderPass::ATTACHMENT_SLOT_COLOR0];
			skybox_stage_color0.info.format = TEXTURE_FORMAT_RGBA16_SFLOAT;
			skybox_stage_color0.info.expected_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			skybox_stage_color0.info.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
			skybox_stage_color0.info.store_op = VK_ATTACHMENT_STORE_OP_STORE;
			skybox_stage_color0.info.clear_value.color = { 0.0f, 0.0f, 0.0f, 1.0f };

			data->render_passes.skybox = std::make_unique<RenderPass>(stages);
		}

		// Geometry pass
		{
			std::vector<RenderPass::Stage> stages(RENDER_PASS_GEOMETRY_NUM_STAGES);
			
			// Depth pre-pass stage
			{
				Vulkan::GraphicsPipelineInfo pipeline_info = {};
				pipeline_info.color_attachment_formats = {};
				pipeline_info.depth_stencil_attachment_format = { TEXTURE_FORMAT_D32_SFLOAT };
				pipeline_info.depth_test = true;
				pipeline_info.depth_write = true;
				pipeline_info.depth_func = VK_COMPARE_OP_LESS_OR_EQUAL;
				pipeline_info.vs_path = "assets/shaders/DepthPrepass.vert";

				pipeline_info.push_ranges.resize(1);
				pipeline_info.push_ranges[0].size = 2 * sizeof(uint32_t);
				pipeline_info.push_ranges[0].offset = 0;
				pipeline_info.push_ranges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

				RenderPass::Stage& depth_prepass_stage = stages[RENDER_PASS_GEOMETRY_STAGE_DEPTH_PREPASS];
				depth_prepass_stage.pipeline = Vulkan::CreateGraphicsPipeline(pipeline_info);

				RenderPass::Attachment& depth_prepass_depth_stencil = depth_prepass_stage.attachments[RenderPass::ATTACHMENT_SLOT_DEPTH_STENCIL];
				depth_prepass_depth_stencil.info.format = TEXTURE_FORMAT_D32_SFLOAT;
				depth_prepass_depth_stencil.info.expected_layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
				depth_prepass_depth_stencil.info.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
				depth_prepass_depth_stencil.info.store_op = VK_ATTACHMENT_STORE_OP_STORE;
				depth_prepass_depth_stencil.info.clear_value.depthStencil = { 1.0f, 0 };
			}

			// Lighting stage
			{
				Vulkan::GraphicsPipelineInfo pipeline_info = {};
				pipeline_info.color_attachment_formats = { TEXTURE_FORMAT_RGBA16_SFLOAT };
				pipeline_info.depth_stencil_attachment_format = { TEXTURE_FORMAT_D32_SFLOAT };
				pipeline_info.depth_test = true;
				pipeline_info.depth_write = false;
				pipeline_info.depth_func = VK_COMPARE_OP_EQUAL;
				pipeline_info.vs_path = "assets/shaders/PbrLighting.vert";
				pipeline_info.fs_path = "assets/shaders/PbrLighting.frag";

				pipeline_info.push_ranges.resize(2);
				pipeline_info.push_ranges[0].size = 2 * sizeof(uint32_t);
				pipeline_info.push_ranges[0].offset = 0;
				pipeline_info.push_ranges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

				pipeline_info.push_ranges[1].size = 8 * sizeof(uint32_t);
				pipeline_info.push_ranges[1].offset = pipeline_info.push_ranges[0].size;
				pipeline_info.push_ranges[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

				RenderPass::Stage& lighting_stage = stages[RENDER_PASS_GEOMETRY_STAGE_LIGHTING];
				lighting_stage.pipeline = Vulkan::CreateGraphicsPipeline(pipeline_info);

				RenderPass::Attachment& lighting_stage_color0 = lighting_stage.attachments[RenderPass::ATTACHMENT_SLOT_COLOR0];
				lighting_stage_color0.info.format = TEXTURE_FORMAT_RGBA16_SFLOAT;
				lighting_stage_color0.info.expected_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				lighting_stage_color0.info.load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
				lighting_stage_color0.info.store_op = VK_ATTACHMENT_STORE_OP_STORE;

				RenderPass::Attachment& lighting_stage_depth_stencil = lighting_stage.attachments[RenderPass::ATTACHMENT_SLOT_DEPTH_STENCIL];
				lighting_stage_depth_stencil.info.format = TEXTURE_FORMAT_D32_SFLOAT;
				lighting_stage_depth_stencil.info.expected_layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
				lighting_stage_depth_stencil.info.load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
				lighting_stage_depth_stencil.info.store_op = VK_ATTACHMENT_STORE_OP_STORE;
			}

			data->render_passes.geometry = std::make_unique<RenderPass>(stages);
		}

		// Post processing pass
		{
			std::vector<RenderPass::Stage> stages(RENDER_PASS_POST_PROCESS_NUM_STAGES);

			// Tonemap, gamma correction and exposure stage
			Vulkan::ComputePipelineInfo pipeline_info = {};
			pipeline_info.cs_path = "assets/shaders/PostProcessCS.glsl";

			pipeline_info.push_ranges.resize(1);
			pipeline_info.push_ranges[0].size = 2 * sizeof(uint32_t);
			pipeline_info.push_ranges[0].offset = 0;
			pipeline_info.push_ranges[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

			RenderPass::Stage& tonemap_stage = stages[RENDER_PASS_POST_PROCESS_STAGE_TONEMAP_GAMMA_EXPOSURE];
			tonemap_stage.pipeline = Vulkan::CreateComputePipeline(pipeline_info);

			RenderPass::Attachment& tonemap_stage_readonly0 = tonemap_stage.attachments[RenderPass::ATTACHMENT_SLOT_READ_ONLY0];
			tonemap_stage_readonly0.info.format = TEXTURE_FORMAT_RGBA16_SFLOAT;
			tonemap_stage_readonly0.info.expected_layout = VK_IMAGE_LAYOUT_GENERAL;
			tonemap_stage_readonly0.info.load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
			tonemap_stage_readonly0.info.store_op = VK_ATTACHMENT_STORE_OP_STORE;

			RenderPass::Attachment& tonemap_stage_readwrite0 = tonemap_stage.attachments[RenderPass::ATTACHMENT_SLOT_READ_WRITE0];
			tonemap_stage_readwrite0.info.format = TEXTURE_FORMAT_RGBA8_UNORM;
			tonemap_stage_readwrite0.info.expected_layout = VK_IMAGE_LAYOUT_GENERAL;
			tonemap_stage_readwrite0.info.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
			tonemap_stage_readwrite0.info.store_op = VK_ATTACHMENT_STORE_OP_STORE;
			tonemap_stage_readwrite0.info.clear_value.color = { 0.0f, 0.0f, 0.0f, 1.0f };

			data->render_passes.post_process = std::make_unique<RenderPass>(stages);
		}

		// Generate IBL cubemaps pass
		{
			std::vector<RenderPass::Stage> stages(RENDER_PASS_GEN_IBL_CUBEMAPS_NUM_STAGES);

			// Generate HDR cubemap stage
			{
				Vulkan::GraphicsPipelineInfo pipeline_info = {};
				pipeline_info.color_attachment_formats = { TEXTURE_FORMAT_RGBA16_SFLOAT };
				pipeline_info.vs_path = "assets/shaders/Cube.vert";
				pipeline_info.fs_path = "assets/shaders/EquirectangularToCube.frag";

				pipeline_info.push_ranges.resize(2);
				pipeline_info.push_ranges[0].size = sizeof(glm::mat4) + sizeof(uint32_t);
				pipeline_info.push_ranges[0].offset = 0;
				pipeline_info.push_ranges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

				pipeline_info.push_ranges[1].size = 2 * sizeof(uint32_t);
				pipeline_info.push_ranges[1].offset = pipeline_info.push_ranges[0].size;
				pipeline_info.push_ranges[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

				RenderPass::Stage& hdr_cubemap_stage = stages[RENDER_PASS_GEN_IBL_CUBEMAPS_STAGE_HDR_CUBEMAP];
				hdr_cubemap_stage.pipeline = Vulkan::CreateGraphicsPipeline(pipeline_info);

				RenderPass::Attachment& hdr_cubemap_color0 = hdr_cubemap_stage.attachments[RenderPass::ATTACHMENT_SLOT_COLOR0];
				hdr_cubemap_color0.info.format = TEXTURE_FORMAT_RGBA16_SFLOAT;
				hdr_cubemap_color0.info.expected_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				hdr_cubemap_color0.info.load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				hdr_cubemap_color0.info.store_op = VK_ATTACHMENT_STORE_OP_STORE;
			}

			// Generate irradiance cubemap stage
			{
				Vulkan::GraphicsPipelineInfo pipeline_info = {};
				pipeline_info.color_attachment_formats = { TEXTURE_FORMAT_RGBA16_SFLOAT };
				pipeline_info.vs_path = "assets/shaders/Cube.vert";
				pipeline_info.fs_path = "assets/shaders/IrradianceCube.frag";

				pipeline_info.push_ranges.resize(2);
				pipeline_info.push_ranges[0].size = sizeof(glm::mat4) + sizeof(uint32_t);
				pipeline_info.push_ranges[0].offset = 0;
				pipeline_info.push_ranges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

				pipeline_info.push_ranges[1].size = 2 * sizeof(uint32_t) + 2 * sizeof(float);
				pipeline_info.push_ranges[1].offset = pipeline_info.push_ranges[0].size;
				pipeline_info.push_ranges[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

				RenderPass::Stage& irradiance_cubemap_stage = stages[RENDER_PASS_GEN_IBL_CUBEMAPS_STAGE_IRRADIANCE_CUBEMAP];
				irradiance_cubemap_stage.pipeline = Vulkan::CreateGraphicsPipeline(pipeline_info);

				RenderPass::Attachment& irradiance_cubemap_color0 = irradiance_cubemap_stage.attachments[RenderPass::ATTACHMENT_SLOT_COLOR0];
				irradiance_cubemap_color0.info.format = TEXTURE_FORMAT_RGBA16_SFLOAT;
				irradiance_cubemap_color0.info.expected_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				irradiance_cubemap_color0.info.load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				irradiance_cubemap_color0.info.store_op = VK_ATTACHMENT_STORE_OP_STORE;
			}

			// Generate prefiltered cubemap stage
			{
				Vulkan::GraphicsPipelineInfo pipeline_info = {};
				pipeline_info.color_attachment_formats = { TEXTURE_FORMAT_RGBA16_SFLOAT };
				pipeline_info.vs_path = "assets/shaders/Cube.vert";
				pipeline_info.fs_path = "assets/shaders/PrefilteredEnvCube.frag";

				pipeline_info.push_ranges.resize(2);
				pipeline_info.push_ranges[0].size = sizeof(glm::mat4) + sizeof(uint32_t);
				pipeline_info.push_ranges[0].offset = 0;
				pipeline_info.push_ranges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

				pipeline_info.push_ranges[1].size = 3 * sizeof(uint32_t) + sizeof(float);
				pipeline_info.push_ranges[1].offset = pipeline_info.push_ranges[0].size;
				pipeline_info.push_ranges[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

				RenderPass::Stage& prefiltered_cubemap_stage = stages[RENDER_PASS_GEN_IBL_CUBEMAPS_STAGE_PREFILTERED_CUBEMAP];
				prefiltered_cubemap_stage.pipeline = Vulkan::CreateGraphicsPipeline(pipeline_info);

				RenderPass::Attachment& prefiltered_cubemap_color0 = prefiltered_cubemap_stage.attachments[RenderPass::ATTACHMENT_SLOT_COLOR0];
				prefiltered_cubemap_color0.info.format = TEXTURE_FORMAT_RGBA16_SFLOAT;
				prefiltered_cubemap_color0.info.expected_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				prefiltered_cubemap_color0.info.load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				prefiltered_cubemap_color0.info.store_op = VK_ATTACHMENT_STORE_OP_STORE;
			}

			data->render_passes.gen_ibl_cubemaps = std::make_unique<RenderPass>(stages);
		}

		// Generate BRDF LUT pass
		{
			std::vector<RenderPass::Stage> stages(RENDER_PASS_BRDF_LUT_NUM_STAGES);

			// BRDF LUT stage
			Vulkan::GraphicsPipelineInfo pipeline_info = {};
			pipeline_info.color_attachment_formats = { TEXTURE_FORMAT_RG16_SFLOAT };
			pipeline_info.vs_path = "assets/shaders/BRDF_LUT.vert";
			pipeline_info.fs_path = "assets/shaders/BRDF_LUT.frag";

			pipeline_info.push_ranges.resize(1);
			pipeline_info.push_ranges[0].size = sizeof(uint32_t);
			pipeline_info.push_ranges[0].offset = 0;
			pipeline_info.push_ranges[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

			RenderPass::Stage& brdf_lut_stage = stages[RENDER_PASS_BRDF_LUT_STAGE_BRDF_LUT];
			brdf_lut_stage.pipeline = Vulkan::CreateGraphicsPipeline(pipeline_info);
			
			RenderPass::Attachment& brdf_lut_color0 = brdf_lut_stage.attachments[RenderPass::ATTACHMENT_SLOT_COLOR0];
			brdf_lut_color0.info.format = TEXTURE_FORMAT_RG16_SFLOAT;
			brdf_lut_color0.info.expected_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			brdf_lut_color0.info.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
			brdf_lut_color0.info.store_op = VK_ATTACHMENT_STORE_OP_STORE;
			brdf_lut_color0.info.clear_value = { 0.0f, 0.0f, 0.0f, 1.0f };

			data->render_passes.gen_brdf_lut = std::make_unique<RenderPass>(stages);
		}

		// Dear ImGui pass
		{
			std::vector<RenderPass::Stage> stages(RENDER_PASS_IMGUI_NUM_STAGES);
			RenderPass::Stage& imgui_stage = stages[RENDER_PASS_IMGUI_STAGE_IMGUI];
			imgui_stage.pipeline.type = VULKAN_PIPELINE_TYPE_GRAPHICS;

			RenderPass::Attachment& imgui_color0 = imgui_stage.attachments[RenderPass::ATTACHMENT_SLOT_COLOR0];
			imgui_color0.info.format = TEXTURE_FORMAT_RGBA8_UNORM;
			imgui_color0.info.expected_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			imgui_color0.info.load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
			imgui_color0.info.store_op = VK_ATTACHMENT_STORE_OP_STORE;

			data->render_passes.imgui = std::make_unique<RenderPass>(stages);
		}
	}

	static RenderResourceHandle GenerateIBLCubemaps(RenderResourceHandle src_texture_handle)
	{
		VulkanCommandBuffer command_buffer = Vulkan::CommandPool::AllocateCommandBuffer(data->command_pools.graphics_compute);
		Vulkan::CommandBuffer::BeginRecording(command_buffer);

		const Mesh* unit_cube_mesh = data->mesh_slotmap.Find(data->unit_cube_mesh_handle);
		VK_ASSERT(unit_cube_mesh && "Unit cube mesh is invalid");

		Texture* hdr_equirect_texture = data->texture_slotmap.Find(src_texture_handle);
		RenderResourceHandle hdr_cubemap_handle, irradiance_cubemap_handle, prefiltered_cubemap_handle;
		Texture* hdr_cubemap, *irradiance_cubemap, *prefiltered_cubemap;

		std::vector<VulkanImageView> temporary_image_views;

		// ----------------------------------------------------------------------------------------------------------------
		// Generate IBL cubemaps Pass (3 stages)
		// 1 - Render equirectangular texture to hdr environment cubemap
		// 2 - Render irradiance cubemap from hdr environment cubemap
		// 3 - Render prefiltered cubemap from hdr environment cubemap

		RENDER_PASS_BEGIN(data->render_passes.gen_ibl_cubemaps);
		{
			// ----------------------------------------------------------------------------------------------------------------
			// 1 - Render equirectangular texture to hdr environment cubemap

			{
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
				VulkanImage hdr_cubemap_image = Vulkan::Image::Create(texture_info);

				TextureViewCreateInfo view_info = {
					.format = texture_info.format,
					.dimension = texture_info.dimension
				};
				VulkanImageView hdr_cubemap_view = Vulkan::ImageView::Create(hdr_cubemap_image, view_info);
				VulkanDescriptorAllocation hdr_cubemap_descriptor = Vulkan::Descriptor::Allocate(VULKAN_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
				Vulkan::Descriptor::Write(hdr_cubemap_descriptor, hdr_cubemap_view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);

				hdr_cubemap_handle = data->texture_slotmap.Emplace(texture_info, hdr_cubemap_image, hdr_cubemap_view, hdr_cubemap_descriptor, data->ibl_sampler);
				hdr_cubemap = data->texture_slotmap.Find(hdr_cubemap_handle);

				// Render all 6 faces of the cube map using 6 different camera view matrices
				VkViewport viewport = {};
				viewport.x = 0.0f, viewport.y = 0.0f;
				viewport.minDepth = 0.0f, viewport.maxDepth = 1.0f;

				VkRect2D scissor_rect = { 0, 0, IBL_HDR_CUBEMAP_RESOLUTION, IBL_HDR_CUBEMAP_RESOLUTION };

				struct PushConsts
				{
					glm::mat4 view_projection;
					uint32_t vb_index;

					uint32_t src_texture_index;
					uint32_t src_sampler_index;
				} push_consts;

				push_consts.vb_index = unit_cube_mesh->vertex_buffer.descriptor.descriptor_offset;

				push_consts.src_texture_index = hdr_equirect_texture->view_descriptor.descriptor_offset;
				push_consts.src_sampler_index = hdr_equirect_texture->sampler.descriptor.descriptor_offset;

				for (uint32_t mip = 0; mip < num_cube_mips; ++mip)
				{
					for (uint32_t face = 0; face < 6; ++face)
					{
						uint32_t render_width = static_cast<float>(IBL_HDR_CUBEMAP_RESOLUTION) * std::pow(0.5f, mip);
						uint32_t render_height = static_cast<float>(IBL_HDR_CUBEMAP_RESOLUTION) * std::pow(0.5f, mip);

						// Render current face to the offscreen render target
						TextureViewCreateInfo face_view_info = {};
						face_view_info.format = texture_info.format;
						face_view_info.dimension = TEXTURE_DIMENSION_2D;
						face_view_info.base_mip = mip;
						face_view_info.num_mips = 1;
						face_view_info.base_layer = face;
						face_view_info.num_layers = 1;

						temporary_image_views.push_back(Vulkan::ImageView::Create(hdr_cubemap_image, face_view_info));
						RENDER_PASS_STAGE_SET_ATTACHMENT(RENDER_PASS_GEN_IBL_CUBEMAPS_STAGE_HDR_CUBEMAP, RenderPass::ATTACHMENT_SLOT_COLOR0, temporary_image_views.back());

						RENDER_PASS_STAGE_BEGIN(RENDER_PASS_GEN_IBL_CUBEMAPS_STAGE_HDR_CUBEMAP, command_buffer, render_width, render_height);

						viewport.width = render_width;
						viewport.height = render_height;

						scissor_rect.extent.width = render_width;
						scissor_rect.extent.height = render_height;

						Vulkan::Command::SetViewport(command_buffer, 0, 1, &viewport);
						Vulkan::Command::SetScissor(command_buffer, 0, 1, &scissor_rect);

						push_consts.view_projection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 512.0f) * CUBE_FACING_VIEW_MATRICES[face];

						Vulkan::Command::PushConstants(command_buffer, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4) + sizeof(uint32_t), &push_consts.view_projection);
						Vulkan::Command::PushConstants(command_buffer, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(glm::mat4) + sizeof(uint32_t), 2 * sizeof(uint32_t), &push_consts.src_texture_index);

						Vulkan::Command::DrawGeometryIndexed(command_buffer, &unit_cube_mesh->index_buffer.buffer,
							unit_cube_mesh->index_buffer.index_type, unit_cube_mesh->index_buffer.num_indices);

						RENDER_PASS_STAGE_END(RENDER_PASS_GEN_IBL_CUBEMAPS_STAGE_HDR_CUBEMAP, command_buffer);
					}
				}

				Vulkan::Command::TransitionLayout(command_buffer, { .image = hdr_cubemap_image, .new_layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL });
			}

			// ----------------------------------------------------------------------------------------------------------------
			// 2 - Render irradiance cubemap from hdr environment cubemap

			{
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
					.name = "Irradiance Cubemap"
				};

				VulkanImage irradiance_cubemap_image = Vulkan::Image::Create(texture_info);

				TextureViewCreateInfo view_info = {
					.format = texture_info.format,
					.dimension = texture_info.dimension
				};
				VulkanImageView irradiance_cubemap_view = Vulkan::ImageView::Create(irradiance_cubemap_image, view_info);
				VulkanDescriptorAllocation irradiance_cubemap_descriptor = Vulkan::Descriptor::Allocate(VULKAN_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
				Vulkan::Descriptor::Write(irradiance_cubemap_descriptor, irradiance_cubemap_view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);

				irradiance_cubemap_handle = data->texture_slotmap.Emplace(texture_info, irradiance_cubemap_image, irradiance_cubemap_view, irradiance_cubemap_descriptor, data->ibl_sampler);
				irradiance_cubemap = data->texture_slotmap.Find(irradiance_cubemap_handle);

				// Render all 6 faces of the cube map using 6 different camera view matrices
				VkViewport viewport = {};
				viewport.x = 0.0f, viewport.y = 0.0f;
				viewport.minDepth = 0.0f, viewport.maxDepth = 1.0f;

				VkRect2D scissor_rect = {};
				scissor_rect.offset.x = 0, scissor_rect.offset.y = 0;

				struct PushConsts
				{
					glm::mat4 view_projection;
					uint32_t vb_index;

					uint32_t src_texture_index;
					uint32_t src_sampler_index;
					float delta_phi = (2.0f * glm::pi<float>()) / 180.0f;
					float delta_theta = (0.5f * glm::pi<float>()) / 64.0f;
				} push_consts;

				push_consts.vb_index = unit_cube_mesh->vertex_buffer.descriptor.descriptor_offset;

				push_consts.delta_phi /= IBL_IRRADIANCE_CUBEMAP_SAMPLE_MULTIPLIER;
				push_consts.delta_theta /= IBL_IRRADIANCE_CUBEMAP_SAMPLE_MULTIPLIER;
				push_consts.src_texture_index = hdr_cubemap->view_descriptor.descriptor_offset;
				push_consts.src_sampler_index = hdr_cubemap->sampler.descriptor.descriptor_offset;

				for (uint32_t mip = 0; mip < num_cube_mips; ++mip)
				{
					for (uint32_t face = 0; face < 6; ++face)
					{
						uint32_t render_width = static_cast<float>(IBL_IRRADIANCE_CUBEMAP_RESOLUTION) * std::pow(0.5f, mip);
						uint32_t render_height = static_cast<float>(IBL_IRRADIANCE_CUBEMAP_RESOLUTION) * std::pow(0.5f, mip);

						// Render current face to the offscreen render target
						TextureViewCreateInfo face_view_info = {};
						face_view_info.format = texture_info.format;
						face_view_info.dimension = TEXTURE_DIMENSION_2D;
						face_view_info.base_mip = mip;
						face_view_info.num_mips = 1;
						face_view_info.base_layer = face;
						face_view_info.num_layers = 1;

						temporary_image_views.push_back(Vulkan::ImageView::Create(irradiance_cubemap_image, face_view_info));
						RENDER_PASS_STAGE_SET_ATTACHMENT(RENDER_PASS_GEN_IBL_CUBEMAPS_STAGE_IRRADIANCE_CUBEMAP, RenderPass::ATTACHMENT_SLOT_COLOR0, temporary_image_views.back());

						RENDER_PASS_STAGE_BEGIN(RENDER_PASS_GEN_IBL_CUBEMAPS_STAGE_IRRADIANCE_CUBEMAP, command_buffer, render_width, render_height);

						viewport.width = render_width;
						viewport.height = render_height;

						scissor_rect.extent.width = render_width;
						scissor_rect.extent.height = render_height;

						Vulkan::Command::SetViewport(command_buffer, 0, 1, &viewport);
						Vulkan::Command::SetScissor(command_buffer, 0, 1, &scissor_rect);

						push_consts.view_projection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 512.0f) * CUBE_FACING_VIEW_MATRICES[face];

						Vulkan::Command::PushConstants(command_buffer, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4) + sizeof(uint32_t), &push_consts.view_projection);
						Vulkan::Command::PushConstants(command_buffer, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(glm::mat4) + sizeof(uint32_t), 2 * sizeof(uint32_t) + 2 * sizeof(float), &push_consts.src_texture_index);

						Vulkan::Command::DrawGeometryIndexed(command_buffer, &unit_cube_mesh->index_buffer.buffer,
							unit_cube_mesh->index_buffer.index_type, unit_cube_mesh->index_buffer.num_indices);

						RENDER_PASS_STAGE_END(RENDER_PASS_GEN_IBL_CUBEMAPS_STAGE_IRRADIANCE_CUBEMAP, command_buffer);
					}
				}

				Vulkan::Command::TransitionLayout(command_buffer, { .image = irradiance_cubemap_image, .new_layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL });
			}

			// ----------------------------------------------------------------------------------------------------------------
			// 3 - Render prefiltered cubemap from hdr environment cubemap

			{
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

				VulkanImage prefiltered_cubemap_image = Vulkan::Image::Create(texture_info);

				TextureViewCreateInfo view_info = {
					.format = texture_info.format,
					.dimension = texture_info.dimension
				};
				VulkanImageView prefiltered_cubemap_view = Vulkan::ImageView::Create(prefiltered_cubemap_image, view_info);
				VulkanDescriptorAllocation prefiltered_descriptor = Vulkan::Descriptor::Allocate(VULKAN_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
				Vulkan::Descriptor::Write(prefiltered_descriptor, prefiltered_cubemap_view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);

				prefiltered_cubemap_handle = data->texture_slotmap.Emplace(texture_info, prefiltered_cubemap_image, prefiltered_cubemap_view, prefiltered_descriptor, data->ibl_sampler);
				prefiltered_cubemap = data->texture_slotmap.Find(prefiltered_cubemap_handle);

				VkViewport viewport = {};
				viewport.x = 0.0f, viewport.y = 0.0f;
				viewport.minDepth = 0.0f, viewport.maxDepth = 1.0f;

				VkRect2D scissor_rect = { 0, 0, IBL_PREFILTERED_CUBEMAP_RESOLUTION, IBL_PREFILTERED_CUBEMAP_RESOLUTION };

				struct PushConsts
				{
					glm::mat4 view_projection;
					uint32_t vb_index;

					uint32_t src_texture_index;
					uint32_t src_sampler_index;
					uint32_t num_samples = IBL_PREFILTERED_CUBEMAP_NUM_SAMPLES;
					float roughness;
				} push_consts;

				push_consts.vb_index = unit_cube_mesh->vertex_buffer.descriptor.descriptor_offset;

				push_consts.src_texture_index = hdr_cubemap->view_descriptor.descriptor_offset;
				push_consts.src_sampler_index = hdr_cubemap->sampler.descriptor.descriptor_offset;

				for (uint32_t mip = 0; mip < num_cube_mips; ++mip)
				{
					uint32_t render_width = static_cast<float>(IBL_PREFILTERED_CUBEMAP_RESOLUTION) * std::pow(0.5f, mip);
					uint32_t render_height = static_cast<float>(IBL_PREFILTERED_CUBEMAP_RESOLUTION) * std::pow(0.5f, mip);

					for (uint32_t face = 0; face < 6; ++face)
					{
						TextureViewCreateInfo face_view_info = {};
						face_view_info.format = texture_info.format;
						face_view_info.dimension = TEXTURE_DIMENSION_2D;
						face_view_info.base_mip = mip;
						face_view_info.num_mips = 1;
						face_view_info.base_layer = face;
						face_view_info.num_layers = 1;

						temporary_image_views.push_back(Vulkan::ImageView::Create(prefiltered_cubemap_image, face_view_info));
						RENDER_PASS_STAGE_SET_ATTACHMENT(RENDER_PASS_GEN_IBL_CUBEMAPS_STAGE_PREFILTERED_CUBEMAP, RenderPass::ATTACHMENT_SLOT_COLOR0, temporary_image_views.back());

						RENDER_PASS_STAGE_BEGIN(RENDER_PASS_GEN_IBL_CUBEMAPS_STAGE_PREFILTERED_CUBEMAP, command_buffer, render_width, render_height);
						{
							viewport.width = render_width;
							viewport.height = render_height;

							scissor_rect.extent.width = render_width;
							scissor_rect.extent.height = render_height;

							Vulkan::Command::SetViewport(command_buffer, 0, 1, &viewport);
							Vulkan::Command::SetScissor(command_buffer, 0, 1, &scissor_rect);

							push_consts.view_projection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 512.0f) * CUBE_FACING_VIEW_MATRICES[face];
							push_consts.roughness = (float)mip / (float)(num_cube_mips - 1);

							Vulkan::Command::PushConstants(command_buffer, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4) + sizeof(uint32_t), &push_consts.view_projection);
							Vulkan::Command::PushConstants(command_buffer, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(glm::mat4) + sizeof(uint32_t), 3 * sizeof(uint32_t) + sizeof(float), &push_consts.src_texture_index);

							Vulkan::Command::DrawGeometryIndexed(command_buffer, &unit_cube_mesh->index_buffer.buffer,
								unit_cube_mesh->index_buffer.index_type, unit_cube_mesh->index_buffer.num_indices);
						}
						RENDER_PASS_STAGE_END(RENDER_PASS_GEN_IBL_CUBEMAPS_STAGE_PREFILTERED_CUBEMAP, command_buffer, data->render_passes.gen_prefiltered_cube);
					}
				}

				Vulkan::Command::TransitionLayout(command_buffer, { .image = prefiltered_cubemap_image, .new_layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL });
			}
		}
		RENDER_PASS_END(data->render_passes.gen_ibl_cubemaps);

		Vulkan::CommandBuffer::EndRecording(command_buffer);
		Vulkan::CommandQueue::ExecuteBlocking(data->command_queues.graphics_compute, command_buffer);
		Vulkan::CommandBuffer::Reset(command_buffer);
		Vulkan::CommandPool::FreeCommandBuffer(data->command_pools.graphics_compute, command_buffer);

		// Free temporary image views
		for (auto& temp_view : temporary_image_views)
		{
			Vulkan::ImageView::Destroy(temp_view);
		}

		// Append the irradiance cubemap and prefiltered cubemap to the hdr cubemap texture
		hdr_cubemap->next = irradiance_cubemap_handle;
		irradiance_cubemap->next = prefiltered_cubemap_handle;

		return hdr_cubemap_handle;
	}

	static void GenerateBRDF_LUT()
	{
		VulkanCommandBuffer command_buffer = Vulkan::CommandPool::AllocateCommandBuffer(data->command_pools.graphics_compute);
		Vulkan::CommandBuffer::BeginRecording(command_buffer);

		// Create the BRDF LUT
		TextureCreateInfo texture_info = {
			.format = TEXTURE_FORMAT_RG16_SFLOAT,
			.usage_flags = TEXTURE_USAGE_RENDER_TARGET | TEXTURE_USAGE_SAMPLED,
			.dimension = TEXTURE_DIMENSION_2D,
			.width = IBL_BRDF_LUT_RESOLUTION,
			.height = IBL_BRDF_LUT_RESOLUTION,
			.num_mips = 1,
			.num_layers = 1,
			.name = "BRDF LUT"
		};

		VulkanImage brdf_lut = Vulkan::Image::Create(texture_info);

		TextureViewCreateInfo view_info = {
			.format = texture_info.format,
			.dimension = texture_info.dimension
		};
		VulkanImageView brdf_lut_view = Vulkan::ImageView::Create(brdf_lut, view_info);
		VulkanDescriptorAllocation brdf_lut_descriptor = Vulkan::Descriptor::Allocate(VULKAN_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
		Vulkan::Descriptor::Write(brdf_lut_descriptor, brdf_lut_view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);

		data->ibl.brdf_lut_handle = data->texture_slotmap.Emplace(texture_info, brdf_lut, brdf_lut_view, brdf_lut_descriptor, data->ibl_sampler);
		
		VkViewport viewport = {};
		viewport.x = 0.0f, viewport.y = 0.0f;
		viewport.minDepth = 0.0f, viewport.maxDepth = 1.0f;

		VkRect2D scissor_rect = {};
		scissor_rect.offset.x = 0.0f, scissor_rect.offset.y = 0.0f;

		struct PushConsts
		{
			uint32_t num_samples = IBL_BRDF_LUT_SAMPLES;
		} push_consts;

		uint32_t render_width = IBL_BRDF_LUT_RESOLUTION;
		uint32_t render_height = IBL_BRDF_LUT_RESOLUTION;

		RENDER_PASS_BEGIN(data->render_passes.gen_brdf_lut);
		RENDER_PASS_STAGE_SET_ATTACHMENT(RENDER_PASS_BRDF_LUT_STAGE_BRDF_LUT, RenderPass::ATTACHMENT_SLOT_COLOR0, brdf_lut_view);
		RENDER_PASS_STAGE_BEGIN(RENDER_PASS_BRDF_LUT_STAGE_BRDF_LUT, command_buffer, render_width, render_height);

		viewport.width = render_width;
		viewport.height = render_height;

		scissor_rect.extent.width = render_width;
		scissor_rect.extent.height = render_height;

		Vulkan::Command::SetViewport(command_buffer, 0, 1, &viewport);
		Vulkan::Command::SetScissor(command_buffer, 0, 1, &scissor_rect);

		Vulkan::Command::PushConstants(command_buffer, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(uint32_t), &push_consts);

		Vulkan::Command::DrawGeometry(command_buffer, 3);

		RENDER_PASS_STAGE_END(RENDER_PASS_BRDF_LUT_STAGE_BRDF_LUT, command_buffer);
		RENDER_PASS_END(data->render_passes.gen_brdf_lut);

		Vulkan::Command::TransitionLayout(command_buffer, { .image = brdf_lut, .new_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });

		Vulkan::CommandBuffer::EndRecording(command_buffer);
		Vulkan::CommandQueue::ExecuteBlocking(data->command_queues.graphics_compute, command_buffer);
		Vulkan::CommandBuffer::Reset(command_buffer);
		Vulkan::CommandPool::FreeCommandBuffer(data->command_pools.graphics_compute, command_buffer);
	}

	void Init(::GLFWwindow* window, uint32_t window_width, uint32_t window_height)
	{
		Vulkan::Init(window, window_width, window_height);

		data = new Data();
		data->glfw_window = window;

		data->render_resolution = { window_width, window_height };
		data->output_resolution = { window_width, window_height };

		data->command_queues.graphics_compute = Vulkan::GetCommandQueue(VULKAN_COMMAND_BUFFER_TYPE_GRAPHICS_COMPUTE);
		data->command_queues.transfer = Vulkan::GetCommandQueue(VULKAN_COMMAND_BUFFER_TYPE_TRANSFER);

		data->command_pools.graphics_compute = Vulkan::CommandPool::Create(data->command_queues.graphics_compute);
		data->command_pools.transfer = Vulkan::CommandPool::Create(data->command_queues.transfer);

		CreateRenderTargets();
		CreateRenderPasses();
		CreateSyncObjects();

		// Init Dear ImGui
		Vulkan::InitImGui(window);

		// Upload imgui font
		VulkanCommandBuffer command_buffer = Vulkan::CommandPool::AllocateCommandBuffer(data->command_pools.graphics_compute);
		Vulkan::CommandBuffer::BeginRecording(command_buffer);

		ImGui_ImplVulkan_CreateFontsTexture(command_buffer.vk_command_buffer);

		Vulkan::CommandBuffer::EndRecording(command_buffer);
		Vulkan::CommandQueue::ExecuteBlocking(data->command_queues.graphics_compute, command_buffer);

		ImGui_ImplVulkan_DestroyFontUploadObjects();

		Vulkan::CommandBuffer::Reset(command_buffer);
		Vulkan::CommandPool::FreeCommandBuffer(data->command_pools.graphics_compute, command_buffer);

		for (uint32_t frame_index = 0; frame_index < Vulkan::MAX_FRAMES_IN_FLIGHT; ++frame_index)
		{
			data->per_frame[frame_index].command_buffer = Vulkan::CommandPool::AllocateCommandBuffer(data->command_pools.graphics_compute);
			data->per_frame[frame_index].ubos.descriptors = Vulkan::Descriptor::Allocate(VULKAN_DESCRIPTOR_TYPE_UNIFORM_BUFFER, RESERVED_DESCRIPTOR_UBO_COUNT, frame_index);
			data->per_frame[frame_index].raytracing.tlas_descriptor = Vulkan::Descriptor::Allocate(VULKAN_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE, 1, frame_index);
		}

		CreateDefaultMeshes();
		CreateDefaultSamplers();
		CreateDefaultTextures();
		CreateDefaultMaterial();
		GenerateBRDF_LUT();

		// Set default render settings
		data->settings.use_direct_light = true;
		data->settings.use_multiscatter = true;

		data->settings.use_pbr_squared_roughness = false;
		data->settings.use_pbr_clearcoat = true;
		data->settings.pbr_diffuse_brdf_model = DIFFUSE_BRDF_MODEL_OREN_NAYAR;

		data->settings.use_ibl = true;
		data->settings.use_ibl_clearcoat = true;
		data->settings.use_ibl_multiscatter = true;

		data->settings.postfx_exposure = 1.5f;
		data->settings.postfx_gamma = 2.2f;
		data->settings.postfx_max_white = 100.0f;

		data->settings.debug_render_mode = DEBUG_RENDER_MODE_NONE;
		data->settings.white_furnace_test = false;
	}

	void Exit()
	{
		// Wait for GPU to be idle before we start the cleanup
		Vulkan::WaitDeviceIdle();

		Vulkan::ExitImGui();

		Vulkan::DestroySampler(data->default_sampler);
		Vulkan::DestroySampler(data->ibl_sampler);
		
		for (uint32_t frame_index = 0; frame_index < Vulkan::MAX_FRAMES_IN_FLIGHT; ++frame_index)
		{
			Vulkan::Descriptor::Free(data->per_frame[frame_index].raytracing.tlas_descriptor, frame_index);
			Vulkan::Descriptor::Free(data->per_frame[frame_index].ubos.descriptors, frame_index);
			Vulkan::CommandPool::FreeCommandBuffer(data->command_pools.graphics_compute, data->per_frame[frame_index].command_buffer);
			Vulkan::Sync::DestroyFence(data->per_frame[frame_index].sync.render_finished_fence);

			Vulkan::Buffer::Destroy(data->per_frame[frame_index].raytracing.tlas);
			Vulkan::Buffer::Destroy(data->per_frame[frame_index].raytracing.tlas_scratch);
			Vulkan::Buffer::Destroy(data->per_frame[frame_index].raytracing.tlas_instance_buffer);
		}

		Vulkan::CommandPool::Destroy(data->command_pools.graphics_compute);
		Vulkan::CommandPool::Destroy(data->command_pools.transfer);

		// Clean up the renderer data
		delete data;
		data = nullptr;

		// Finally, exit the vulkan render backend
		Vulkan::Exit();
	}

	void BeginFrame(const BeginFrameInfo& frame_info)
	{
		Frame* frame = GetFrameCurrent();

		// Wait for the current frame to finish rendering, reset command buffer and begin recording
		Vulkan::CommandQueue::WaitFenceValue(data->command_queues.graphics_compute, frame->sync.frame_in_flight_fence_value);
		Vulkan::CommandBuffer::Reset(frame->command_buffer);
		Vulkan::CommandBuffer::BeginRecording(frame->command_buffer);

		// Release TLAS buffers
		Vulkan::Buffer::Destroy(frame->raytracing.tlas);
		Vulkan::Buffer::Destroy(frame->raytracing.tlas_scratch);
		Vulkan::Buffer::Destroy(frame->raytracing.tlas_instance_buffer);

		bool resized = Vulkan::BeginFrame();

		if (resized)
		{
			Vulkan::GetOutputResolution(data->output_resolution.width, data->output_resolution.height);
			data->render_resolution = data->output_resolution;

			CreateRenderTargets();
			return;
		}

		// Set UBO data for the current frame, like camera data and settings
		GPUCamera camera_data = {};
		camera_data.view = frame_info.camera_view;
		camera_data.proj = glm::perspectiveFov(glm::radians(frame_info.camera_vfov),
			(float)data->render_resolution.width, (float)data->render_resolution.height, data->camera_settings.near_plane, data->camera_settings.far_plane);
		camera_data.proj[1][1] *= -1.0f;
		camera_data.view_pos = glm::inverse(frame_info.camera_view)[3];

		// Allocate frame UBOs from ring buffer
		frame->ubos.settings_ubo = data->ring_buffer.Allocate(sizeof(RenderSettings), alignof(RenderSettings));
		frame->ubos.camera_ubo = data->ring_buffer.Allocate(sizeof(GPUCamera), alignof(GPUCamera));
		frame->ubos.light_ubo = data->ring_buffer.Allocate(3 * sizeof(uint32_t) + sizeof(GPUAreaLight) * MAX_AREA_LIGHTS);
		frame->ubos.material_ubo = data->ring_buffer.Allocate(sizeof(GPUMaterial) * MAX_UNIQUE_MATERIALS);

		// Write UBO descriptors
		Vulkan::Descriptor::Write(frame->ubos.descriptors, frame->ubos.settings_ubo.buffer, RESERVED_DESCRIPTOR_UBO_SETTINGS);
		Vulkan::Descriptor::Write(frame->ubos.descriptors, frame->ubos.camera_ubo.buffer, RESERVED_DESCRIPTOR_UBO_CAMERA);
		Vulkan::Descriptor::Write(frame->ubos.descriptors, frame->ubos.light_ubo.buffer, RESERVED_DESCRIPTOR_UBO_LIGHTS);
		Vulkan::Descriptor::Write(frame->ubos.descriptors, frame->ubos.material_ubo.buffer, RESERVED_DESCRIPTOR_UBO_MATERIALS);

		// Write camera data to the camera UBO
		frame->ubos.camera_ubo.WriteBuffer(0, sizeof(GPUCamera), &camera_data);
		frame->ubos.settings_ubo.WriteBuffer(0, sizeof(data->settings), &data->settings);

		// Free the previous instance buffer descriptor if valid
		if (Vulkan::Descriptor::IsValid(frame->instance_buffer.descriptor))
			Vulkan::Descriptor::Free(frame->instance_buffer.descriptor);

		// Allocate instance buffer from ring buffer and allocate/write the descriptor
		frame->instance_buffer.alloc = data->ring_buffer.Allocate(sizeof(InstanceData) * MAX_DRAW_LIST_ENTRIES);
		frame->instance_buffer.descriptor = Vulkan::Descriptor::Allocate(VULKAN_DESCRIPTOR_TYPE_STORAGE_BUFFER);
		Vulkan::Descriptor::Write(frame->instance_buffer.descriptor, frame->instance_buffer.alloc.buffer);

		// If white furnace test is enabled or the skybox texture is invalid,
		// we want to use the white furnace environment map to render instead of the one passed in
		if (data->settings.white_furnace_test ||
			!data->texture_slotmap.Find(frame_info.skybox_texture_handle))
			data->skybox_texture_handle = data->white_furnace_skybox_handle;
		else
			data->skybox_texture_handle = frame_info.skybox_texture_handle;
	}

	void RenderFrame()
	{
		Frame* frame = GetFrameCurrent();

		// Before we start rendering anything, we need to build the TLAS for the current frame
		uint32_t num_blas_meshes = data->draw_list.next_free_entry;
		std::vector<VulkanBuffer> mesh_blas_buffers(num_blas_meshes);
		std::vector<VkTransformMatrixKHR> mesh_transforms(num_blas_meshes);

		for (uint32_t i = 0; i < data->draw_list.next_free_entry; ++i)
		{
			const DrawList::Entry& entry = data->draw_list.entries[i];
			VK_ASSERT(entry.mesh && "Tried to build the TLAS with an invalid mesh");

			mesh_blas_buffers[i] = entry.mesh->blas_buffer;
			memcpy(&mesh_transforms[i], &entry.instance_data.transform, sizeof(VkTransformMatrixKHR));
		}

		frame->raytracing.tlas = Vulkan::Raytracing::BuildTLAS(frame->command_buffer, frame->raytracing.tlas_scratch, frame->raytracing.tlas_instance_buffer,
			num_blas_meshes, mesh_blas_buffers.data(), mesh_transforms.data(), "TLAS Scene");
		Vulkan::Descriptor::Write(frame->raytracing.tlas_descriptor, frame->raytracing.tlas);

		// Update number of lights in the light ubo
		Texture* ltc1_texture = data->texture_slotmap.Find(data->ltc_matrices_texture_handle);
		Texture* ltc2_texture = data->texture_slotmap.Find(data->ltc_ggx_fresnel_sphere_clipping_texture_handle);

		frame->ubos.light_ubo.WriteBuffer(0, sizeof(uint32_t), &data->num_area_lights);
		frame->ubos.light_ubo.WriteBuffer(sizeof(uint32_t), sizeof(uint32_t), &ltc1_texture->view_descriptor.descriptor_offset);
		frame->ubos.light_ubo.WriteBuffer(2 * sizeof(uint32_t), sizeof(uint32_t), &ltc2_texture->view_descriptor.descriptor_offset);

		// Viewport and scissor rect
		VkViewport viewport = {};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(data->render_resolution.width);
		viewport.height = static_cast<float>(data->render_resolution.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		VkRect2D scissor_rect = {};
		scissor_rect.offset = { 0, 0 };
		scissor_rect.extent = { data->render_resolution.width, data->render_resolution.height };

		// ----------------------------------------------------------------------------------------------------------------
		// Skybox Pass (1 stage)
		// 1 - Render the skybox cube

		RENDER_PASS_BEGIN(data->render_passes.skybox);
		{
			RENDER_PASS_STAGE_SET_ATTACHMENT(RENDER_PASS_SKYBOX_STAGE_SKYBOX, RenderPass::ATTACHMENT_SLOT_COLOR0, data->render_targets.hdr.view);
			
			RENDER_PASS_STAGE_BEGIN(RENDER_PASS_SKYBOX_STAGE_SKYBOX, frame->command_buffer, data->render_resolution.width, data->render_resolution.height);

			Vulkan::Command::SetViewport(frame->command_buffer, 0, 1, &viewport);
			Vulkan::Command::SetScissor(frame->command_buffer, 0, 1, &scissor_rect);

			struct PushConsts
			{
				uint32_t vb_index;

				uint32_t env_texture_index;
				uint32_t env_sampler_index;
			} push_consts;

			const Texture* skybox_texture = data->texture_slotmap.Find(data->skybox_texture_handle);
			VK_ASSERT(skybox_texture && "Skybox cubemap is invalid for currently selected skybox");

			const Mesh* unit_cube_mesh = data->mesh_slotmap.Find(data->unit_cube_mesh_handle);

			push_consts.vb_index = unit_cube_mesh->vertex_buffer.descriptor.descriptor_offset;
			Vulkan::Command::PushConstants(frame->command_buffer, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(uint32_t), &push_consts.vb_index);

			push_consts.env_texture_index = skybox_texture->view_descriptor.descriptor_offset;
			push_consts.env_sampler_index = data->default_sampler.descriptor.descriptor_offset;
			Vulkan::Command::PushConstants(frame->command_buffer, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(uint32_t), 2 * sizeof(uint32_t), &push_consts.env_texture_index);

			Vulkan::Command::DrawGeometryIndexed(frame->command_buffer, &unit_cube_mesh->index_buffer.buffer,
				unit_cube_mesh->index_buffer.index_type, unit_cube_mesh->index_buffer.num_indices);

			RENDER_PASS_STAGE_END(RENDER_PASS_SKYBOX_STAGE_SKYBOX, frame->command_buffer);
		}
		RENDER_PASS_END(data->render_passes.skybox);

		// ----------------------------------------------------------------------------------------------------------------
		// Geometry Pass (1 stage)
		// 1 - Depth pre-pass stage
		// 2 - Render geometry and evaluate lighting
		// 3 - TODO: Transparent objects forward rendering stage

		RENDER_PASS_BEGIN(data->render_passes.geometry);
		{
			// Depth pre-pass stage
			{
				RENDER_PASS_STAGE_SET_ATTACHMENT(RENDER_PASS_GEOMETRY_STAGE_DEPTH_PREPASS, RenderPass::ATTACHMENT_SLOT_DEPTH_STENCIL, data->render_targets.depth.view);

				RENDER_PASS_STAGE_BEGIN(RENDER_PASS_GEOMETRY_STAGE_DEPTH_PREPASS, frame->command_buffer, data->render_resolution.width, data->render_resolution.height);

				Vulkan::Command::SetViewport(frame->command_buffer, 0, 1, &viewport);
				Vulkan::Command::SetScissor(frame->command_buffer, 0, 1, &scissor_rect);

				struct PushConsts
				{
					uint32_t ib_index;
					uint32_t vb_index;
				} push;

				push.ib_index = frame->instance_buffer.descriptor.descriptor_offset;
				Vulkan::Command::PushConstants(frame->command_buffer, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(uint32_t), &push.ib_index);

				for (uint32_t i = 0; i < data->draw_list.next_free_entry; ++i)
				{
					// NOTE: When we do vertex pulling instead, we can store the vertex/index buffer descriptor indices inside the instance data
					// And then we could simply render all meshes with a single draw call
					const DrawList::Entry& entry = data->draw_list.entries[i];
					VK_ASSERT(entry.mesh && "Tried to render a mesh with an invalid mesh handle");

					push.vb_index = entry.mesh->vertex_buffer.descriptor.descriptor_offset;
					Vulkan::Command::PushConstants(frame->command_buffer, VK_SHADER_STAGE_VERTEX_BIT, sizeof(uint32_t), sizeof(uint32_t), &push.vb_index);

					Vulkan::Command::DrawGeometryIndexed(frame->command_buffer, &entry.mesh->index_buffer.buffer,
						entry.mesh->index_buffer.index_type, entry.mesh->index_buffer.num_indices, 1, i);
				}

				RENDER_PASS_STAGE_END(RENDER_PASS_GEOMETRY_STAGE_DEPTH_PREPASS, frame->command_buffer);
			}

			// Geometry and lighting stage
			{
				RENDER_PASS_STAGE_SET_ATTACHMENT(RENDER_PASS_GEOMETRY_STAGE_LIGHTING, RenderPass::ATTACHMENT_SLOT_COLOR0, data->render_targets.hdr.view);
				RENDER_PASS_STAGE_SET_ATTACHMENT(RENDER_PASS_GEOMETRY_STAGE_LIGHTING, RenderPass::ATTACHMENT_SLOT_DEPTH_STENCIL, data->render_targets.depth.view);

				RENDER_PASS_STAGE_BEGIN(RENDER_PASS_GEOMETRY_STAGE_LIGHTING, frame->command_buffer, data->render_resolution.width, data->render_resolution.height);

				// Viewport and scissor
				Vulkan::Command::SetViewport(frame->command_buffer, 0, 1, &viewport);
				Vulkan::Command::SetScissor(frame->command_buffer, 0, 1, &scissor_rect);

				const Texture* skybox_texture = data->texture_slotmap.Find(data->skybox_texture_handle);
				VK_ASSERT(skybox_texture && "Skybox cubemap is invalid for currently selected skybox");

				const Texture* irradiance_cubemap = data->texture_slotmap.Find(skybox_texture->next);
				VK_ASSERT(irradiance_cubemap && "Irradiance cubemap is invalid for currently selected skybox");

				const Texture* prefiltered_cubemap = data->texture_slotmap.Find(irradiance_cubemap->next);
				VK_ASSERT(prefiltered_cubemap && "Prefiltered cubemap is invalid for currently selected skybox");

				const Texture* brdf_lut = data->texture_slotmap.Find(data->ibl.brdf_lut_handle);
				VK_ASSERT(brdf_lut && "BRDF LUT is invalid");

				// Push constants
				struct PushConsts
				{
					uint32_t ib_index;
					uint32_t vb_index;

					uint32_t irradiance_cubemap_index;
					uint32_t irradiance_sampler_index;
					uint32_t prefiltered_cubemap_index;
					uint32_t prefiltered_sampler_index;
					uint32_t num_prefiltered_mips;
					uint32_t brdf_lut_index;
					uint32_t brdf_lut_sampler_index;
					uint32_t tlas_index;
				} push_consts;

				push_consts.ib_index = frame->instance_buffer.descriptor.descriptor_offset;
				Vulkan::Command::PushConstants(frame->command_buffer, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(uint32_t), &push_consts.ib_index);

				push_consts.irradiance_cubemap_index = irradiance_cubemap->view_descriptor.descriptor_offset;
				push_consts.irradiance_sampler_index = irradiance_cubemap->sampler.descriptor.descriptor_offset;
				push_consts.prefiltered_cubemap_index = prefiltered_cubemap->view_descriptor.descriptor_offset;
				push_consts.prefiltered_sampler_index = prefiltered_cubemap->sampler.descriptor.descriptor_offset;
				push_consts.num_prefiltered_mips = prefiltered_cubemap->view.num_mips - 1;
				push_consts.brdf_lut_index = brdf_lut->view_descriptor.descriptor_offset;
				push_consts.brdf_lut_sampler_index = brdf_lut->sampler.descriptor.descriptor_offset;
				push_consts.tlas_index = frame->raytracing.tlas_descriptor.descriptor_offset;

				Vulkan::Command::PushConstants(frame->command_buffer, VK_SHADER_STAGE_FRAGMENT_BIT, 2 * sizeof(uint32_t), 8 * sizeof(uint32_t), &push_consts.irradiance_cubemap_index);

				for (uint32_t i = 0; i < data->draw_list.next_free_entry; ++i)
				{
					const DrawList::Entry& entry = data->draw_list.entries[i];
					VK_ASSERT(entry.mesh && "Tried to render a mesh with an invalid mesh handle");

					push_consts.vb_index = entry.mesh->vertex_buffer.descriptor.descriptor_offset;
					Vulkan::Command::PushConstants(frame->command_buffer, VK_SHADER_STAGE_VERTEX_BIT, sizeof(uint32_t), sizeof(uint32_t), &push_consts.vb_index);
					
					Vulkan::Command::DrawGeometryIndexed(frame->command_buffer, &entry.mesh->index_buffer.buffer,
						entry.mesh->index_buffer.index_type, entry.mesh->index_buffer.num_indices, 1, i);

					data->stats.total_vertex_count += entry.mesh->vertex_buffer.buffer.size_in_bytes / sizeof(Vertex);
					data->stats.total_triangle_count += entry.mesh->index_buffer.num_indices / 3;
				}

				RENDER_PASS_STAGE_END(RENDER_PASS_GEOMETRY_STAGE_LIGHTING, frame->command_buffer);
			}
		}
		RENDER_PASS_END(data->render_passes.geometry);

		// ----------------------------------------------------------------------------------------------------------------
		// Post-process Pass (1 stage)
		// 1 - Tonemapping, gamma correction, exposure

		RENDER_PASS_BEGIN(data->render_passes.post_process);
		{
			RENDER_PASS_STAGE_SET_ATTACHMENT(RENDER_PASS_POST_PROCESS_STAGE_TONEMAP_GAMMA_EXPOSURE, RenderPass::ATTACHMENT_SLOT_READ_ONLY0, data->render_targets.hdr.view);
			RENDER_PASS_STAGE_SET_ATTACHMENT(RENDER_PASS_POST_PROCESS_STAGE_TONEMAP_GAMMA_EXPOSURE, RenderPass::ATTACHMENT_SLOT_READ_WRITE0, data->render_targets.sdr.view);

			RENDER_PASS_STAGE_BEGIN(RENDER_PASS_POST_PROCESS_STAGE_TONEMAP_GAMMA_EXPOSURE, frame->command_buffer, data->render_resolution.width, data->render_resolution.height);

			struct PushConsts
			{
				uint32_t hdr_src_index;
				uint32_t sdr_dst_index;
			} push_consts;

			push_consts.hdr_src_index = data->render_targets.hdr.descriptor.descriptor_offset;
			push_consts.sdr_dst_index = data->render_targets.sdr.descriptor.descriptor_offset;

			Vulkan::Command::PushConstants(frame->command_buffer, VK_SHADER_STAGE_COMPUTE_BIT, 0, 2 * sizeof(uint32_t), &push_consts);

			uint32_t dispatch_x = VK_ALIGN_POW2(data->render_resolution.width, 8) / 8;
			uint32_t dispatch_y = VK_ALIGN_POW2(data->render_resolution.height, 8) / 8;
			Vulkan::Command::Dispatch(frame->command_buffer, dispatch_x, dispatch_y, 1);

			RENDER_PASS_STAGE_END(RENDER_PASS_POST_PROCESS_STAGE_TONEMAP_GAMMA_EXPOSURE, frame->command_buffer);
		}
		RENDER_PASS_END(data->render_passes.post_process);
	}

	void RenderUI()
	{
		if (ImGui::Begin("Renderer"))
		{
			ImGui::Text("Total vertex count: %u", data->stats.total_vertex_count);
			ImGui::Text("Total triangle count: %u", data->stats.total_triangle_count);

			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
			if (ImGui::CollapsingHeader("Settings"))
			{
				ImGui::Indent(10.0f);

				bool vsync = Vulkan::SwapChain::IsVSyncEnabled();
				if (ImGui::Checkbox("VSync", &vsync))
				{
					Vulkan::SwapChain::SetVSync(vsync);
				}

				// ------------------------------------------------------------------------------------------------------
				// Debug settings

				ImGui::SetNextItemOpen(false, ImGuiCond_Once);
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

					ImGui::Checkbox("White furnace test", (bool*)&data->settings.white_furnace_test);
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("If enabled, switches the HDR environment for a purely white uniformly lit environment");
					}

					ImGui::Unindent(10.0f);
				}

				// ------------------------------------------------------------------------------------------------------
				// PBR settings

				ImGui::SetNextItemOpen(false, ImGuiCond_Once);
				if (ImGui::CollapsingHeader("PBR"))
				{
					ImGui::Indent(10.0f);

					ImGui::Checkbox("Use direct light", (bool*)&data->settings.use_direct_light);
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("If enabled, evaluates direct lighting from light sources");
					}

					ImGui::Checkbox("Use multiscatter", (bool*)&data->settings.use_multiscatter);
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("If enabled, specular direct lighting will be energy conserving, taking multiscatter specular bounces between microfacets into account");
					}

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

					ImGui::SetNextItemOpen(false, ImGuiCond_Once);
					if (ImGui::CollapsingHeader("IBL"))
					{
						ImGui::Indent(10.0f);

						ImGui::Checkbox("Use IBL", (bool*)&data->settings.use_ibl);
						if (ImGui::IsItemHovered())
						{
							ImGui::SetTooltip("Toggle image-based lighting");
						}
						ImGui::Checkbox("Use IBL clearcoat", (bool*)&data->settings.use_ibl_clearcoat);
						if (ImGui::IsItemHovered())
						{
							ImGui::SetTooltip("If enabled, clearcoat materials will have their own specular lobe when evaluating specular indirect lighting");
						}
						ImGui::Checkbox("Use IBL multiscatter", (bool*)&data->settings.use_ibl_multiscatter);
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

				ImGui::SetNextItemOpen(false, ImGuiCond_Once);
				if (ImGui::CollapsingHeader("Post-processing"))
				{
					ImGui::Indent(10.0f);

					ImGui::SliderFloat("Exposure", &data->settings.postfx_exposure, 0.001f, 20.0f, "%.2f");
					ImGui::SliderFloat("Gamma", &data->settings.postfx_gamma, 0.001f, 20.0f, "%.2f");
					ImGui::SliderFloat("Max white", &data->settings.postfx_max_white, 0.1f, 1000.0f, "%.2f");

					ImGui::Unindent(10.0f);
				}

				// ------------------------------------------------------------------------------------------------------
				// Camera settings

				ImGui::SetNextItemOpen(false, ImGuiCond_Once);
				if (ImGui::CollapsingHeader("Camera"))
				{
					ImGui::Indent(10.0f);

					ImGui::DragFloat("Camera near plane", &data->camera_settings.near_plane, 0.1f, 0.1f, 10000.0f);
					ImGui::DragFloat("Camera far plane", &data->camera_settings.far_plane, 0.1f, 0.1f, 10000.0f);

					ImGui::Unindent(10.0f);
				}

				ImGui::Unindent(10.0f);
			}
		}
		ImGui::End();
	}

	void EndFrame()
	{
		Frame* frame = GetFrameCurrent();

		// ----------------------------------------------------------------------------------------------------------------
		// Dear ImGui Pass (1 stage)
		// 1 - Dear ImGui

		RENDER_PASS_BEGIN(data->render_passes.imgui);
		{
			RENDER_PASS_STAGE_SET_ATTACHMENT(RENDER_PASS_IMGUI_STAGE_IMGUI, RenderPass::ATTACHMENT_SLOT_COLOR0, data->render_targets.sdr.view);

			RENDER_PASS_STAGE_BEGIN(RENDER_PASS_IMGUI_STAGE_IMGUI, frame->command_buffer, data->render_resolution.width, data->render_resolution.height);

			ImGui::Render();
			ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), frame->command_buffer.vk_command_buffer, nullptr);

			ImGui::EndFrame();

			RENDER_PASS_STAGE_END(RENDER_PASS_IMGUI_STAGE_IMGUI, frame->command_buffer);
		}
		RENDER_PASS_END(data->render_passes.imgui);

		// Copy the final rendered frame to the back buffer
		Vulkan::CopyToBackBuffer(frame->command_buffer, data->render_targets.sdr.image);

		// End recording commands, execute command buffer
		Vulkan::CommandBuffer::EndRecording(frame->command_buffer);
		frame->sync.frame_in_flight_fence_value = Vulkan::CommandQueue::Execute(data->command_queues.graphics_compute, frame->command_buffer, 1, &frame->sync.render_finished_fence);

		// Vulkan backend end frame, does the swapchain present
		bool resized = Vulkan::EndFrame(frame->sync.render_finished_fence);

		// Reset per-frame statistics, draw list, and other data
		data->stats.Reset();
		data->draw_list.Reset();
		data->num_area_lights = 0;

		if (resized)
		{
			Vulkan::GetOutputResolution(data->output_resolution.width, data->output_resolution.height);
			data->render_resolution = data->output_resolution;

			CreateRenderTargets();
		}
	}

	RenderResourceHandle CreateTexture(const CreateTextureArgs& args)
	{
		VulkanCommandBuffer command_buffer = Vulkan::CommandPool::AllocateCommandBuffer(data->command_pools.graphics_compute);
		Vulkan::CommandBuffer::BeginRecording(command_buffer);

		// Determine the texture byte size
		VkDeviceSize image_size = args.width * args.height * args.src_stride;

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

		VulkanImage image = Vulkan::Image::Create(texture_info);

		// Create ring buffer staging allocation and write the data to it
		RingBuffer::Allocation staging = data->ring_buffer.Allocate(image_size, Vulkan::Image::GetMemoryRequirements(image).alignment);
		staging.WriteBuffer(0, image_size, args.pixel_bytes.data());
		
		// Copy staging buffer data into final texture image memory (device local)
		Vulkan::Command::TransitionLayout(command_buffer, { .image = image, .new_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL });
		Vulkan::Command::CopyFromBuffer(command_buffer, staging.buffer, staging.buffer.offset_in_bytes, image, texture_info.width, texture_info.height);

		// Generate mips
		if (num_mips > 1)
			Vulkan::Command::GenerateMips(command_buffer, image);
		else
			// Generate Mips will already transition the image back to READ_ONLY_OPTIMAL, if we do not generate mips, we have to do it manually
			Vulkan::Command::TransitionLayout(command_buffer, { .image = image, .new_layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL });

		TextureViewCreateInfo view_info = {
			.format = texture_info.format,
			.dimension = texture_info.dimension
		};
		VulkanImageView view = Vulkan::ImageView::Create(image, view_info);

		VulkanDescriptorAllocation view_descriptor = Vulkan::Descriptor::Allocate(VULKAN_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
		Vulkan::Descriptor::Write(view_descriptor, view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);

		Vulkan::CommandBuffer::EndRecording(command_buffer);
		Vulkan::CommandQueue::ExecuteBlocking(data->command_queues.graphics_compute, command_buffer);
		Vulkan::CommandBuffer::Reset(command_buffer);
		Vulkan::CommandPool::FreeCommandBuffer(data->command_pools.graphics_compute, command_buffer);

		RenderResourceHandle texture_handle = data->texture_slotmap.Emplace(texture_info, image, view, view_descriptor, data->default_sampler);

		// If the texture is  an environment map, further processing is required
		// Generate textures required for image-based lighting from the HDR equirectangular texture
		if (args.is_environment_map)
		{
			RenderResourceHandle original_texture_handle = texture_handle;

			// Generate IBL cubemaps from the original equirectangular cubemap
			texture_handle = GenerateIBLCubemaps(original_texture_handle);

			// Destroy the original equirectangular hdr environment texture
			data->texture_slotmap.Delete(original_texture_handle);
		}

		return texture_handle;
	}

	void DestroyTexture(RenderResourceHandle handle)
	{
		while (VK_RESOURCE_HANDLE_VALID(handle))
		{
			VK_ASSERT(VK_RESOURCE_HANDLE_VALID(handle) && "Tried to destroy a texture with an invalid texture handle");

			RenderResourceHandle next_handle = data->texture_slotmap.Find(handle)->next;
			data->texture_slotmap.Delete(handle);
			handle = next_handle;
		}
	}

	void ImGuiImage(RenderResourceHandle texture_handle, float width, float height)
	{
		const Texture* texture = data->texture_slotmap.Find(texture_handle);
		if (!texture || !texture->imgui_descriptor_set)
			texture = data->texture_slotmap.Find(data->default_white_texture_handle);

		ImGui::Image(texture->imgui_descriptor_set, { width, height });
	}

	void ImGuiImageButton(RenderResourceHandle texture_handle, float width, float height)
	{
		const Texture* texture = data->texture_slotmap.Find(texture_handle);
		if (!texture || !texture->imgui_descriptor_set)
			texture = data->texture_slotmap.Find(data->default_white_texture_handle);

		ImGui::ImageButton(texture->imgui_descriptor_set, { width, height });
	}

	RenderResourceHandle CreateMesh(const CreateMeshArgs& args)
	{
		// Determine vertex and index buffer byte size
		VkDeviceSize vb_size = args.vertices_bytes.size();
		VkDeviceSize ib_size = args.indices_bytes.size();

		// Allocate from ring buffer and write data to it
		RingBuffer::Allocation staging = data->ring_buffer.Allocate(vb_size + ib_size);
		staging.WriteBuffer(0, vb_size, args.vertices_bytes.data());
		staging.WriteBuffer(vb_size, ib_size, args.indices_bytes.data());

		// Create vertex and index buffers, and storage buffer descriptors (vertex pulling)
		VertexBuffer vertex_buffer = {};
		vertex_buffer.buffer = Vulkan::Buffer::CreateVertex(vb_size, "Vertex Buffer " + args.name);
		vertex_buffer.descriptor = Vulkan::Descriptor::Allocate(VULKAN_DESCRIPTOR_TYPE_STORAGE_BUFFER);
		Vulkan::Descriptor::Write(vertex_buffer.descriptor, vertex_buffer.buffer);

		IndexBuffer index_buffer = {};
		index_buffer.buffer = Vulkan::Buffer::CreateIndex(ib_size, "Index Buffer " + args.name);
		index_buffer.index_type = Vulkan::Util::ToVkIndexType(args.index_stride);
		index_buffer.num_indices = args.num_indices;

		// Copy staging buffer data into vertex and index buffers
		VulkanCommandBuffer command_buffer = Vulkan::CommandPool::AllocateCommandBuffer(data->command_pools.graphics_compute);
		Vulkan::CommandBuffer::BeginRecording(command_buffer);

		Vulkan::Command::CopyBuffers(command_buffer, staging.buffer, 0, vertex_buffer.buffer, 0, vb_size);
		Vulkan::Command::CopyBuffers(command_buffer, staging.buffer, vb_size, index_buffer.buffer, 0, ib_size);

		std::vector<VulkanBufferBarrier> copy_to_acceleration_structure_build_barriers =
		{
			{ vertex_buffer.buffer, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			  VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR },
			{ index_buffer.buffer, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			  VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR }
		};
		Vulkan::Command::BufferMemoryBarriers(command_buffer, copy_to_acceleration_structure_build_barriers);
		
		VulkanBuffer blas_scratch_buffer = {};
		VulkanBuffer blas_buffer = Vulkan::Raytracing::BuildBLAS(command_buffer, vertex_buffer.buffer, index_buffer.buffer, blas_scratch_buffer,
			args.num_vertices, sizeof(Vertex), args.num_indices / 3, Vulkan::Util::ToVkIndexType(args.index_stride), "BLAS " + args.name);

		std::vector<VulkanBufferBarrier> acceleration_structure_build_to_vertex_index_barriers =
		{
			{ vertex_buffer.buffer, VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
			  VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT },
			{ index_buffer.buffer, VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
			  VK_ACCESS_2_INDEX_READ_BIT, VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT }
		};
		Vulkan::Command::BufferMemoryBarriers(command_buffer, copy_to_acceleration_structure_build_barriers);

		Vulkan::CommandBuffer::EndRecording(command_buffer);
		Vulkan::CommandQueue::ExecuteBlocking(data->command_queues.graphics_compute, command_buffer);
		Vulkan::CommandBuffer::Reset(command_buffer);
		Vulkan::CommandPool::FreeCommandBuffer(data->command_pools.graphics_compute, command_buffer);

		Vulkan::Buffer::Destroy(blas_scratch_buffer);

		return data->mesh_slotmap.Emplace(
			vertex_buffer, index_buffer, blas_buffer
		);
	}

	void DestroyMesh(RenderResourceHandle handle)
	{
		data->mesh_slotmap.Delete(handle);
	}

	void SubmitMesh(RenderResourceHandle mesh_handle, const MaterialAsset& material, const glm::mat4& transform)
	{
		DrawList::Entry& entry = data->draw_list.GetNextEntry();
		// Get the mesh from the slotmap. If it does not exist/is invalid, use the unit cube mesh as a default placeholder
		entry.mesh = data->mesh_slotmap.Find(mesh_handle);
		if (!entry.mesh)
			entry.mesh = data->mesh_slotmap.Find(data->unit_cube_mesh_handle);

		memcpy(&entry.instance_data.transform, &transform[0][0], sizeof(glm::mat4));
		entry.instance_data.material_index = entry.index;

		// Write mesh transform to the instance buffer for the currently active frame
		Frame* frame = GetFrameCurrent();
		frame->instance_buffer.alloc.WriteBuffer(sizeof(InstanceData) * entry.index, sizeof(InstanceData), &entry.instance_data);
		entry.gpu_material = data->default_gpu_material;

		// Write material data to the material UBO
		Texture* albedo_texture = data->texture_slotmap.Find(material.tex_albedo_render_handle);
		if (albedo_texture)
			entry.gpu_material.albedo_texture_index = albedo_texture->view_descriptor.descriptor_offset;

		Texture* normal_texture = data->texture_slotmap.Find(material.tex_normal_render_handle);
		if (normal_texture)
			entry.gpu_material.normal_texture_index = normal_texture->view_descriptor.descriptor_offset;

		Texture* metallic_roughness_texture = data->texture_slotmap.Find(material.tex_metal_rough_render_handle);
		if (metallic_roughness_texture)
			entry.gpu_material.metallic_roughness_texture_index = metallic_roughness_texture->view_descriptor.descriptor_offset;

		entry.gpu_material.albedo_factor = material.albedo_factor;
		entry.gpu_material.metallic_factor = material.metallic_factor;
		entry.gpu_material.roughness_factor = material.roughness_factor;

		entry.gpu_material.has_clearcoat = material.has_clearcoat ? 1 : 0;

		Texture* clearcoat_alpha_texture = data->texture_slotmap.Find(material.tex_cc_alpha_render_handle);
		if (clearcoat_alpha_texture)
			entry.gpu_material.clearcoat_alpha_texture_index = clearcoat_alpha_texture->view_descriptor.descriptor_offset;

		Texture* clearcoat_normal_texture = data->texture_slotmap.Find(material.tex_cc_normal_render_handle);
		if (clearcoat_normal_texture)
			entry.gpu_material.clearcoat_normal_texture_index = clearcoat_normal_texture->view_descriptor.descriptor_offset;

		Texture* clearcoat_roughness_texture = data->texture_slotmap.Find(material.tex_cc_rough_render_handle);
		if (clearcoat_roughness_texture)
			entry.gpu_material.clearcoat_roughness_texture_index = clearcoat_roughness_texture->view_descriptor.descriptor_offset;

		entry.gpu_material.clearcoat_alpha_factor = material.clearcoat_alpha_factor;
		entry.gpu_material.clearcoat_roughness_factor = material.clearcoat_roughness_factor;

		// Default sampler
		entry.gpu_material.sampler_index = data->default_sampler.descriptor.descriptor_offset;

		// Write material data to the material ubo for the currently active frame
		frame->ubos.material_ubo.WriteBuffer(sizeof(GPUMaterial) * entry.index, sizeof(GPUMaterial), &entry.gpu_material);
	}

	void SubmitPointlight(const glm::vec3& pos, const glm::vec3& color, float intensity)
	{
		return;
		/*VK_ASSERT(data->num_light_sources < MAX_LIGHT_SOURCES && "Exceeded the maximum amount of point lights");

		GPULightSource pointlight = {
			.pos = pos,
			.intensity = intensity,
			.color = color
		};

		Frame* frame = GetFrameCurrent();
		frame->ubos.light_ubo.WriteBuffer(sizeof(PointlightData) * data->num_pointlights, sizeof(PointlightData), &pointlight);
		data->num_pointlights++;*/
	}

	void SubmitAreaLight(RenderResourceHandle texture_handle, const glm::mat4& transform, const glm::vec3& color, float intensity, bool two_sided)
	{
		VK_ASSERT(data->num_area_lights < MAX_AREA_LIGHTS && "Exceeded the maximum amount of area lights");

		// Add area light to be drawn as a mesh
		DrawList::Entry& entry = data->draw_list.GetNextEntry();
		entry.mesh = data->mesh_slotmap.Find(data->unit_quad_mesh_handle);

		memcpy(&entry.instance_data.transform, &transform[0][0], sizeof(glm::mat4));
		entry.instance_data.material_index = entry.index;

		Frame* frame = GetFrameCurrent();
		frame->instance_buffer.alloc.WriteBuffer(sizeof(InstanceData) * entry.index, sizeof(InstanceData), &entry.instance_data);

		entry.gpu_material = data->default_gpu_material;
		entry.gpu_material.albedo_factor = glm::vec4(color * intensity, 1.0f);
		entry.gpu_material.blackbody_radiator = true;
		Texture* albedo_texture = data->texture_slotmap.Find(texture_handle);
		if (albedo_texture)
			entry.gpu_material.albedo_texture_index = albedo_texture->view_descriptor.descriptor_offset;

		// Write material data to the material ubo for the currently active frame
		frame->ubos.material_ubo.WriteBuffer(sizeof(GPUMaterial) * entry.index, sizeof(GPUMaterial), &entry.gpu_material);

		// Add GPU data representation for the area light to the light UBO
		glm::vec3 quad_points[4] =
		{
			glm::vec3(UNIT_QUAD_VERTICES[0].pos[0], UNIT_QUAD_VERTICES[0].pos[1], UNIT_QUAD_VERTICES[0].pos[2]),
			glm::vec3(UNIT_QUAD_VERTICES[1].pos[0], UNIT_QUAD_VERTICES[1].pos[1], UNIT_QUAD_VERTICES[1].pos[2]),
			glm::vec3(UNIT_QUAD_VERTICES[3].pos[0], UNIT_QUAD_VERTICES[3].pos[1], UNIT_QUAD_VERTICES[3].pos[2]),
			glm::vec3(UNIT_QUAD_VERTICES[2].pos[0], UNIT_QUAD_VERTICES[2].pos[1], UNIT_QUAD_VERTICES[2].pos[2]),
		};

		GPUAreaLight gpu_area_light = {};
		gpu_area_light.vert0 = glm::vec3(transform * glm::vec4(quad_points[0], 1.0f));
		gpu_area_light.color_red = color.r;
		gpu_area_light.vert1 = glm::vec3(transform * glm::vec4(quad_points[1], 1.0f));
		gpu_area_light.color_green = color.g;
		gpu_area_light.vert2 = glm::vec3(transform * glm::vec4(quad_points[2], 1.0f));
		gpu_area_light.color_blue = color.b;
		gpu_area_light.vert3 = glm::vec3(transform * glm::vec4(quad_points[3], 1.0f));
		gpu_area_light.intensity = intensity;
		gpu_area_light.two_sided = two_sided;

		if (albedo_texture)
			gpu_area_light.texture_index = albedo_texture->view_descriptor.descriptor_offset;

		frame->ubos.light_ubo.WriteBuffer(4 * sizeof(uint32_t) + data->num_area_lights * sizeof(GPUAreaLight), sizeof(GPUAreaLight), &gpu_area_light);
		data->num_area_lights++;
	}

}
