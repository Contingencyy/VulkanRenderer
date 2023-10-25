#include "renderer/Renderer.h"
#include "renderer/VulkanBackend.h"
#include "renderer/DescriptorBuffer.h"
#include "renderer/ResourceSlotmap.h"
#include "renderer/RenderPass.h"
#include "Common.h"
#include "Shared.glsl.h"

#include "vulkan/vulkan.h"
#include "vulkan/vk_enum_string_helper.h"

#include "GLFW/glfw3.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_vulkan.h"

#include <array>
#include <optional>
#include <assert.h>
#include <cstring>
#include <set>
#include <algorithm>
#include <exception>
#include <numeric>
#include <string>

namespace Renderer
{

	static constexpr uint32_t MAX_DRAW_LIST_ENTRIES = 10000;

	static const std::vector<VkFormat> TEXTURE_FORMAT_TO_VK_FORMAT = { VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_SRGB };

	struct DrawList
	{
		struct Entry
		{
			MeshHandle_t mesh_handle;
			MaterialHandle_t material_handle;
			glm::mat4 transform;
		};

		size_t current_entry = 0;
		std::array<Entry, MAX_DRAW_LIST_ENTRIES> entries;

		Entry& GetNextEntry()
		{
			VK_ASSERT(current_entry < MAX_DRAW_LIST_ENTRIES &&
				"Exceeded the maximum amount of draw list entries");

			Entry& entry = entries[current_entry];
			current_entry++;
			return entry;
		}

		void Reset()
		{
			current_entry = 0;
		}
	};

	struct Data
	{
		::GLFWwindow* window = nullptr;

		// Resource slotmaps
		ResourceSlotmap<TextureResource> texture_slotmap;
		ResourceSlotmap<MeshResource> mesh_slotmap;
		ResourceSlotmap<MaterialResource> material_slotmap;

		// Command buffers and synchronization primitives
		std::vector<VkCommandBuffer> command_buffers;
		std::vector<VkSemaphore> render_finished_semaphores;
		std::vector<VkFence> in_flight_fences;

		// Descriptor buffers
		DescriptorBuffer descriptor_buffer_uniform{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, (uint32_t)vk_inst.descriptor_sizes.uniform_buffer };
		DescriptorBuffer descriptor_buffer_storage{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, (uint32_t)vk_inst.descriptor_sizes.storage_buffer };
		DescriptorBuffer descriptor_buffer_storage_image{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, (uint32_t)vk_inst.descriptor_sizes.storage_image };
		DescriptorBuffer descriptor_buffer_sampled_image{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, (uint32_t)vk_inst.descriptor_sizes.sampled_image };
		DescriptorBuffer descriptor_buffer_sampler{ VK_DESCRIPTOR_TYPE_SAMPLER, (uint32_t)vk_inst.descriptor_sizes.sampler };

		// Render passes
		struct RenderPasses
		{
			RenderPass lighting{ RenderPassType_Graphics };
			RenderPass post_process{ RenderPassType_Compute };
		} render_passes;

		TextureHandle_t hdr_color_target_handle;
		TextureResource* hdr_color_target = nullptr;
		TextureHandle_t depth_target_handle;
		TextureResource* depth_target = nullptr;
		TextureHandle_t sdr_color_target_handle;
		TextureResource* sdr_color_target = nullptr;

		DescriptorAllocation reserved_storage_image_descriptors;

		// Draw submission list
		DrawList draw_list;

		// Uniform buffers
		// TODO: Free reserved descriptors on Exit()
		DescriptorAllocation reserved_ubo_descriptors;
		std::vector<Vulkan::Buffer> camera_uniform_buffers;
		std::vector<Vulkan::Buffer> light_uniform_buffers;
		uint32_t num_light_sources = 0;

		std::vector<Vulkan::Buffer> instance_buffers;

		// Storage buffers
		// TODO: Free reserved descriptors on Exit()
		DescriptorAllocation reserved_storage_descriptors;
		Vulkan::Buffer material_buffer;

		// Default resources
		TextureHandle_t default_white_texture_handle;
		TextureHandle_t default_normal_texture_handle;
		VkSampler default_sampler = VK_NULL_HANDLE;
		// TODO: Free reserved descriptors on Exit()
		DescriptorAllocation reserved_sampler_descriptors;

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
			RenderPass render_pass{ RenderPassType_Graphics };
		} imgui;
	} static *data;
	
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
		data->command_buffers.resize(VulkanInstance::MAX_FRAMES_IN_FLIGHT);

		VkCommandBufferAllocateInfo alloc_info = {};
		alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		alloc_info.commandPool = vk_inst.cmd_pools.graphics;
		alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		alloc_info.commandBufferCount = (uint32_t)data->command_buffers.size();

		VkCheckResult(vkAllocateCommandBuffers(vk_inst.device, &alloc_info, data->command_buffers.data()));
	}

	static void CreateSyncObjects()
	{
		data->render_finished_semaphores.resize(VulkanInstance::MAX_FRAMES_IN_FLIGHT);
		data->in_flight_fences.resize(VulkanInstance::MAX_FRAMES_IN_FLIGHT);

		VkSemaphoreCreateInfo semaphore_info = {};
		semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		VkFenceCreateInfo fence_info = {};
		fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

		for (size_t i = 0; i < VulkanInstance::MAX_FRAMES_IN_FLIGHT; ++i)
		{
			VkCheckResult(vkCreateSemaphore(vk_inst.device, &semaphore_info, nullptr, &data->render_finished_semaphores[i]));
			VkCheckResult(vkCreateFence(vk_inst.device, &fence_info, nullptr, &data->in_flight_fences[i]));
		}
	}

	static void CreateDefaultSamplers()
	{
		// Create sampler
		VkSamplerCreateInfo sampler_info = {};
		sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		// Oversampling
		sampler_info.magFilter = VK_FILTER_LINEAR;
		// Undersampling
		sampler_info.minFilter = VK_FILTER_LINEAR;
		sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sampler_info.anisotropyEnable = VK_TRUE;
		sampler_info.maxAnisotropy = vk_inst.device_props.max_anisotropy;
		sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		sampler_info.unnormalizedCoordinates = VK_FALSE;
		sampler_info.compareEnable = VK_FALSE;
		sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
		sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampler_info.mipLodBias = 0.0f;
		sampler_info.minLod = 0.0f;
		//sampler_info.maxLod = (float)num_mips;
		sampler_info.maxLod = std::numeric_limits<float>::max();

		VkCheckResult(vkCreateSampler(vk_inst.device, &sampler_info, nullptr, &data->default_sampler));

		// Allocate and update sampler descriptor
		data->reserved_sampler_descriptors = data->descriptor_buffer_sampler.Allocate();
		data->reserved_sampler_descriptors.WriteDescriptor(data->default_sampler);
	}

	static void CreateDefaultTextures()
	{
		// Default white texture
		std::vector<uint8_t> white_pixel = { 0xFF, 0xFF, 0xFF, 0xFF };

		CreateTextureArgs texture_args = {};
		texture_args.width = 1;
		texture_args.height = 1;
		texture_args.pixels = white_pixel;

		data->default_white_texture_handle = CreateTexture(texture_args);

		// Default normal texture
		std::vector<uint8_t> normal_pixel = { 127, 127, 255, 255 };
		texture_args.pixels = normal_pixel;

		data->default_normal_texture_handle = CreateTexture(texture_args);
	}

	static void CreateUniformBuffers()
	{
		data->camera_uniform_buffers.resize(VulkanInstance::MAX_FRAMES_IN_FLIGHT);
		VkDeviceSize camera_buffer_size = sizeof(CameraData);

		data->light_uniform_buffers.resize(VulkanInstance::MAX_FRAMES_IN_FLIGHT);
		VkDeviceSize light_buffer_size = MAX_LIGHT_SOURCES * sizeof(PointlightData);

		for (size_t i = 0; i < VulkanInstance::MAX_FRAMES_IN_FLIGHT; ++i)
		{
			Vulkan::CreateUniformBuffer(camera_buffer_size, data->camera_uniform_buffers[i]);
			data->reserved_ubo_descriptors.WriteDescriptor(data->camera_uniform_buffers[i],
				camera_buffer_size, RESERVED_DESCRIPTOR_UNIFORM_BUFFER_CAMERA * VulkanInstance::MAX_FRAMES_IN_FLIGHT + i);

			Vulkan::CreateUniformBuffer(light_buffer_size, data->light_uniform_buffers[i]);
			data->reserved_ubo_descriptors.WriteDescriptor(data->light_uniform_buffers[i],
				light_buffer_size, RESERVED_DESCRIPTOR_UNIFORM_BUFFER_LIGHT_SOURCES * VulkanInstance::MAX_FRAMES_IN_FLIGHT + i);
		}
	}

	static void CreateInstanceBuffers()
	{
		data->instance_buffers.resize(VulkanInstance::MAX_FRAMES_IN_FLIGHT);
		VkDeviceSize instance_buffer_size = MAX_DRAW_LIST_ENTRIES * sizeof(glm::mat4);

		for (size_t i = 0; i < VulkanInstance::MAX_FRAMES_IN_FLIGHT; ++i)
		{
			Vulkan::Buffer& instance_buffer = data->instance_buffers[i];
			Vulkan::CreateBuffer(instance_buffer_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, instance_buffer);
			VkCheckResult(vkMapMemory(vk_inst.device, instance_buffer.memory, 0, instance_buffer_size, 0, &instance_buffer.ptr));
		}
	}

	static void CreateMaterialBuffer()
	{
		VkDeviceSize material_buffer_size = MAX_UNIQUE_MATERIALS * sizeof(MaterialResource);
		Vulkan::CreateBuffer(material_buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT |
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, data->material_buffer);
		data->reserved_storage_descriptors.WriteDescriptor(data->material_buffer, material_buffer_size, RESERVED_DESCRIPTOR_STORAGE_BUFFER_MATERIAL);
	}

	static void CreateRenderTargets()
	{
		// Create HDR render target
		{
			if (!data->hdr_color_target)
			{
				ResourceSlotmap<TextureResource>::ReservedResource reserved = data->texture_slotmap.Reserve();
				data->hdr_color_target_handle = reserved.handle;
				data->hdr_color_target = reserved.resource;
			}

			Vulkan::CreateImage(
				vk_inst.swapchain.extent.width, vk_inst.swapchain.extent.height,
				VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				data->hdr_color_target->image
			);
			Vulkan::CreateImageView(VK_IMAGE_ASPECT_COLOR_BIT, data->hdr_color_target->image);
			data->reserved_storage_image_descriptors.WriteDescriptor(data->hdr_color_target->image, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, RESERVED_DESCRIPTOR_STORAGE_IMAGE_HDR);
		}

		// Create depth render target
		{
			if (!data->depth_target)
			{
				ResourceSlotmap<TextureResource>::ReservedResource reserved = data->texture_slotmap.Reserve();
				data->depth_target_handle = reserved.handle;
				data->depth_target = reserved.resource;
			}

			Vulkan::CreateImage(
				vk_inst.swapchain.extent.width, vk_inst.swapchain.extent.height,
				Vulkan::FindDepthFormat(),
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				data->depth_target->image
			);
			Vulkan::CreateImageView(VK_IMAGE_ASPECT_DEPTH_BIT, data->depth_target->image);
		}

		// Create SDR render target
		{
			if (!data->sdr_color_target)
			{
				ResourceSlotmap<TextureResource>::ReservedResource reserved = data->texture_slotmap.Reserve();
				data->sdr_color_target_handle = reserved.handle;
				data->sdr_color_target = reserved.resource;
			}

			Vulkan::CreateImage(
				vk_inst.swapchain.extent.width, vk_inst.swapchain.extent.height,
				vk_inst.swapchain.format,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				data->sdr_color_target->image
			);
			Vulkan::CreateImageView(VK_IMAGE_ASPECT_COLOR_BIT, data->sdr_color_target->image);
			data->reserved_storage_image_descriptors.WriteDescriptor(data->sdr_color_target->image, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, RESERVED_DESCRIPTOR_STORAGE_IMAGE_SDR);
		}
	}

	static void DestroyRenderTargets()
	{
		Vulkan::DestroyImage(data->hdr_color_target->image);
		Vulkan::DestroyImage(data->depth_target->image);
		Vulkan::DestroyImage(data->sdr_color_target->image);
	}

	static void CreateRenderPasses()
	{
		data->reserved_storage_image_descriptors = data->descriptor_buffer_storage_image.Allocate(RESERVED_DESCRIPTOR_STORAGE_IMAGE_COUNT);
		CreateRenderTargets();

		std::vector<VkDescriptorSetLayout> descriptor_set_layouts =
		{
			data->descriptor_buffer_uniform.GetDescriptorSetLayout(),
			data->descriptor_buffer_storage.GetDescriptorSetLayout(),
			data->descriptor_buffer_storage_image.GetDescriptorSetLayout(),
			data->descriptor_buffer_sampled_image.GetDescriptorSetLayout(),
			data->descriptor_buffer_sampler.GetDescriptorSetLayout()
		};

		// Lighting raster pass
		{
			std::vector<RenderPass::Attachment> attachments(2);
			attachments[0].info.attachment_type = RenderPass::AttachmentType_Color;
			attachments[0].info.expected_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			attachments[0].info.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachments[0].info.store_op = VK_ATTACHMENT_STORE_OP_STORE;
			attachments[0].info.clear_value.color = { 0.0f, 0.0f, 0.0f, 1.0f };
			attachments[0].resource = data->hdr_color_target;

			attachments[1].info.attachment_type = RenderPass::AttachmentType_DepthStencil;
			attachments[1].info.expected_layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
			attachments[1].info.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachments[1].info.store_op = VK_ATTACHMENT_STORE_OP_STORE;
			attachments[1].info.clear_value.depthStencil = { 1.0f, 0 };
			attachments[1].resource = data->depth_target;

			data->render_passes.lighting.SetAttachments(attachments);

			std::vector<VkPushConstantRange> push_ranges(1);
			push_ranges[0].size = 4 * sizeof(uint32_t);
			push_ranges[0].offset = 0;
			push_ranges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

			Vulkan::GraphicsPipelineInfo info = {};
			info.input_bindings = GetVertexBindingDescription();
			info.input_attributes = GetVertexAttributeDescription();
			info.color_attachment_formats = data->render_passes.lighting.GetColorAttachmentFormats();
			info.depth_enabled = true;
			info.depth_stencil_attachment_format = data->render_passes.lighting.GetDepthStencilAttachmentFormat();
			info.vs_path = "assets/shaders/VertexShader.vert";
			info.fs_path = "assets/shaders/FragmentShader.frag";

			data->render_passes.lighting.Build(descriptor_set_layouts, push_ranges, info);
		}

		// Post-processing compute pass
		{
			std::vector<RenderPass::Attachment> attachments(2);
			attachments[0].info.attachment_type = RenderPass::AttachmentType_ReadOnly;
			attachments[0].info.expected_layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
			attachments[0].resource = data->hdr_color_target;

			attachments[1].info.attachment_type = RenderPass::AttachmentType_Color;
			attachments[1].info.expected_layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
			attachments[1].info.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachments[1].info.store_op = VK_ATTACHMENT_STORE_OP_STORE;
			attachments[1].resource = data->sdr_color_target;
			attachments[1].info.clear_value.color = { 0.0f, 0.0f, 0.0f, 1.0f };

			data->render_passes.post_process.SetAttachments(attachments);

			std::vector<VkPushConstantRange> push_ranges(1);
			push_ranges[0].size = 2 * sizeof(uint32_t);
			push_ranges[0].offset = 0;
			push_ranges[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

			Vulkan::ComputePipelineInfo info = {};
			info.cs_path = "assets/shaders/PostProcessCS.glsl";

			data->render_passes.post_process.Build(descriptor_set_layouts, push_ranges, info);
		}

		// Dear ImGui render pass
		{
			std::vector<RenderPass::Attachment> attachments(1);
			attachments[0].info.attachment_type = RenderPass::AttachmentType_Color;
			attachments[0].info.expected_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			attachments[0].info.load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
			attachments[0].info.store_op = VK_ATTACHMENT_STORE_OP_STORE;
			attachments[0].resource = data->sdr_color_target;

			data->imgui.render_pass.SetAttachments(attachments);
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
		VkDescriptorPoolSize pool_size = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
		VkDescriptorPoolCreateInfo pool_info = {};
		pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		pool_info.maxSets = 1;
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
		init_info.ColorAttachmentFormat = data->sdr_color_target->image.format;
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

	void Init(::GLFWwindow* window)
	{
		Vulkan::Init(window);

		data = new Data();
		data->window = window;

		CreateRenderPasses();
		InitDearImGui();

		CreateCommandBuffers();
		CreateSyncObjects();

		data->reserved_ubo_descriptors = data->descriptor_buffer_uniform.Allocate(RESERVED_DESCRIPTOR_UNIFORM_BUFFER_COUNT * VulkanInstance::MAX_FRAMES_IN_FLIGHT);
		CreateUniformBuffers();
		data->reserved_storage_descriptors = data->descriptor_buffer_storage.Allocate(RESERVED_DESCRIPTOR_STORAGE_BUFFER_COUNT);
		CreateMaterialBuffer();
		CreateInstanceBuffers();

		CreateDefaultSamplers();
		CreateDefaultTextures();
	}

	void Exit()
	{
		vkDeviceWaitIdle(vk_inst.device);

		ExitDearImGui();

		// Clean up vulkan stuff
		for (size_t i = 0; i < VulkanInstance::MAX_FRAMES_IN_FLIGHT; ++i)
		{
			vkDestroyFence(vk_inst.device, data->in_flight_fences[i], nullptr);
			vkDestroySemaphore(vk_inst.device, data->render_finished_semaphores[i], nullptr);
		}

		vkDestroySampler(vk_inst.device, data->default_sampler, nullptr);

		for (size_t i = 0; i < VulkanInstance::VulkanInstance::MAX_FRAMES_IN_FLIGHT; ++i)
		{
			Vulkan::DestroyBuffer(data->camera_uniform_buffers[i]);
			Vulkan::DestroyBuffer(data->light_uniform_buffers[i]);
			Vulkan::DestroyBuffer(data->instance_buffers[i]);
		}
		Vulkan::DestroyBuffer(data->material_buffer);

		// Clean up the renderer data
		delete data;

		// Finally, exit the vulkan render backend
		Vulkan::Exit();
	}

	void BeginFrame(const glm::mat4& view, const glm::mat4& proj)
	{
		// Wait for completion of all rendering for the current swapchain image
		VkCheckResult(vkWaitForFences(vk_inst.device, 1, &data->in_flight_fences[vk_inst.current_frame], VK_TRUE, UINT64_MAX));

		// Reset the fence
		VkCheckResult(vkResetFences(vk_inst.device, 1, &data->in_flight_fences[vk_inst.current_frame]));

		// Get an available image index from the swap chain
		bool resized = Vulkan::SwapChainAcquireNextImage();
		if (resized)
		{
			DestroyRenderTargets();
			CreateRenderTargets();
			return;
		}

		// Reset and record the command buffer
		VkCommandBuffer graphics_command_buffer = data->command_buffers[vk_inst.current_frame];
		vkResetCommandBuffer(graphics_command_buffer, 0);

		ImGui_ImplGlfw_NewFrame();
		ImGui_ImplVulkan_NewFrame();
		ImGui::NewFrame();

		CameraData ubo = {};
		ubo.view = view;
		ubo.proj = proj;
		ubo.view_pos = glm::inverse(view)[3];

		memcpy(data->camera_uniform_buffers[vk_inst.current_frame].ptr, &ubo, sizeof(ubo));
	}

	void RenderFrame()
	{
		VkCommandBuffer command_buffer = data->command_buffers[vk_inst.current_frame];

		VkCommandBufferBeginInfo command_buffer_begin_info = {};
		command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		VkCheckResult(vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info));

		// ----------------------------------------------------------------------------------------------------------------
		// Lighting pass

		data->render_passes.lighting.Begin(command_buffer);

		// Viewport and scissor
		VkViewport viewport = {};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = (float)vk_inst.swapchain.extent.width;
		viewport.height = (float)vk_inst.swapchain.extent.height;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(command_buffer, 0, 1, &viewport);

		VkRect2D scissor = {};
		scissor.offset = { 0, 0 };
		scissor.extent = vk_inst.swapchain.extent;
		vkCmdSetScissor(command_buffer, 0, 1, &scissor);

		// Push constants
		uint32_t ubo_indices[] = { RESERVED_DESCRIPTOR_UNIFORM_BUFFER_CAMERA * VulkanInstance::MAX_FRAMES_IN_FLIGHT + vk_inst.current_frame,
			RESERVED_DESCRIPTOR_UNIFORM_BUFFER_LIGHT_SOURCES * VulkanInstance::MAX_FRAMES_IN_FLIGHT + vk_inst.current_frame, data->num_light_sources };
		data->render_passes.lighting.PushConstants(command_buffer, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 3 * sizeof(uint32_t), ubo_indices);

		// Bind descriptor buffers
		std::array<VkDescriptorBufferBindingInfoEXT, 5> descriptor_buffer_binding_infos =
		{
			data->descriptor_buffer_uniform.GetBindingInfo(),
			data->descriptor_buffer_storage.GetBindingInfo(),
			data->descriptor_buffer_storage_image.GetBindingInfo(),
			data->descriptor_buffer_sampled_image.GetBindingInfo(),
			data->descriptor_buffer_sampler.GetBindingInfo()
		};
		vk_inst.pFunc.cmd_bind_descriptor_buffers_ext(command_buffer, (uint32_t)descriptor_buffer_binding_infos.size(), descriptor_buffer_binding_infos.data());

		uint32_t buffer_indices[5] = { 0, 1, 2, 3, 4 };
		VkDeviceSize buffer_offsets[5] = { 0, 0, 0, 0, 0 };
		data->render_passes.lighting.SetDescriptorBufferOffsets(command_buffer, 0, 5, &buffer_indices[0], &buffer_offsets[0]);

		// Instance buffer
		const Vulkan::Buffer& instance_buffer = data->instance_buffers[vk_inst.current_frame];

		for (size_t i = 0; i < data->draw_list.current_entry; ++i)
		{
			const DrawList::Entry& entry = data->draw_list.entries[i];

			// TODO: Default mesh
			MeshResource* mesh = nullptr;
			if (VK_RESOURCE_HANDLE_VALID(entry.mesh_handle))
			{
				mesh = data->mesh_slotmap.Find(entry.mesh_handle);
			}
			VK_ASSERT(mesh && "Tried to render a mesh with an invalid mesh handle");

			// Check material handles for validity
			VK_ASSERT(VK_RESOURCE_HANDLE_VALID(entry.material_handle));
			MaterialResource* material = data->material_slotmap.Find(entry.material_handle);
			VK_ASSERT(VK_RESOURCE_HANDLE_VALID(material->base_color_texture_handle));
			VK_ASSERT(VK_RESOURCE_HANDLE_VALID(material->normal_texture_handle));
			VK_ASSERT(VK_RESOURCE_HANDLE_VALID(material->metallic_roughness_texture_handle));

			data->render_passes.lighting.PushConstants(command_buffer, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 12, sizeof(uint32_t), &entry.material_handle.index);

			// Vertex and index buffers
			VkBuffer vertex_buffers[] = { mesh->vertex_buffer.buffer, instance_buffer.buffer };
			VkDeviceSize offsets[] = { 0, i * sizeof(glm::mat4) };
			vkCmdBindVertexBuffers(command_buffer, 0, 2, vertex_buffers, offsets);
			vkCmdBindIndexBuffer(command_buffer, mesh->index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);

			// Draw call
			vkCmdDrawIndexed(command_buffer, mesh->index_buffer.size / sizeof(uint32_t), 1, 0, 0, 0);

			data->stats.total_vertex_count += mesh->vertex_buffer.size / sizeof(Vertex);
			data->stats.total_triangle_count += (mesh->index_buffer.size / sizeof(uint32_t)) / 3;
		}
		data->render_passes.lighting.End(command_buffer);

		// ----------------------------------------------------------------------------------------------------------------
		// Post-process pass

		data->render_passes.post_process.Begin(command_buffer);

		vk_inst.pFunc.cmd_bind_descriptor_buffers_ext(command_buffer, (uint32_t)descriptor_buffer_binding_infos.size(), descriptor_buffer_binding_infos.data());
		data->render_passes.post_process.SetDescriptorBufferOffsets(command_buffer, 0, 5, &buffer_indices[0], &buffer_offsets[0]);

		uint32_t src_dst_indices[2] = { RESERVED_DESCRIPTOR_STORAGE_IMAGE_HDR, RESERVED_DESCRIPTOR_STORAGE_IMAGE_SDR };
		data->render_passes.post_process.PushConstants(command_buffer, VK_SHADER_STAGE_COMPUTE_BIT, 0, 2 * sizeof(uint32_t), &src_dst_indices);
		uint32_t dispatch_x = VK_ALIGN_POW2(vk_inst.swapchain.extent.width, 8) / 8;
		uint32_t dispatch_y = VK_ALIGN_POW2(vk_inst.swapchain.extent.height, 8) / 8;
		vkCmdDispatch(command_buffer, dispatch_x, dispatch_y, 1);

		data->render_passes.post_process.End(command_buffer);
	}

	void RenderUI()
	{
		ImGui::Begin("Renderer");

		ImGui::Text("Total vertex count: %u", data->stats.total_vertex_count);
		ImGui::Text("Total triangle count: %u", data->stats.total_triangle_count);

		ImGui::End();
	}

	void EndFrame()
	{
		VkCommandBuffer command_buffer = data->command_buffers[vk_inst.current_frame];

		// Render ImGui
		data->imgui.render_pass.Begin(command_buffer);

		ImGui::Render();
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command_buffer, nullptr);

		data->imgui.render_pass.End(command_buffer);

		// Copy final result to swapchain image
		VkImageCopy copy_region = {};
		copy_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copy_region.srcSubresource.mipLevel = 0;
		copy_region.srcSubresource.baseArrayLayer = 0;
		copy_region.srcSubresource.layerCount = 1;
		copy_region.srcOffset = { 0, 0, 0 };
		copy_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copy_region.dstSubresource.mipLevel = 0;
		copy_region.dstSubresource.baseArrayLayer = 0;
		copy_region.dstSubresource.layerCount = 1;
		copy_region.dstOffset = { 0, 0, 0 };
		copy_region.extent = { vk_inst.swapchain.extent.width, vk_inst.swapchain.extent.height, 1 };

		// Note: Temporary hack
		Vulkan::Image swapchain_image = Vulkan::SwapChainGetCurrentImage();
		
		std::vector<VkImageMemoryBarrier2> swapchain_copy_begin_transitions =
		{
			Vulkan::ImageMemoryBarrier(data->sdr_color_target->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
			Vulkan::ImageMemoryBarrier(swapchain_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
		};
		Vulkan::CmdTransitionImageLayouts(command_buffer, swapchain_copy_begin_transitions);

		vkCmdCopyImage(command_buffer, data->sdr_color_target->image.image, data->sdr_color_target->image.layout,
			swapchain_image.image, swapchain_image.layout, 1, &copy_region);

		Vulkan::CmdTransitionImageLayout(command_buffer, swapchain_image, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

		VkCheckResult(vkEndCommandBuffer(command_buffer));

		// Submit the command buffer for execution
		VkSubmitInfo submit_info = {};
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

		VkSemaphore wait_semaphores[] = { vk_inst.swapchain.image_available_semaphores[vk_inst.current_frame] };
		VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		submit_info.waitSemaphoreCount = 1;
		submit_info.pWaitSemaphores = wait_semaphores;
		submit_info.pWaitDstStageMask = wait_stages;
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &command_buffer;

		std::vector<VkSemaphore> signal_semaphores = { data->render_finished_semaphores[vk_inst.current_frame] };
		submit_info.signalSemaphoreCount = (uint32_t)signal_semaphores.size();
		submit_info.pSignalSemaphores = signal_semaphores.data();

		VkCheckResult(vkQueueSubmit(vk_inst.queues.graphics, 1, &submit_info, data->in_flight_fences[vk_inst.current_frame]));

		// Present
		bool resized = Vulkan::SwapChainPresent(signal_semaphores);
		if (resized)
		{
			DestroyRenderTargets();
			CreateRenderTargets();
		}

		ImGui::EndFrame();

		// Reset/Update per-frame data
		data->stats.Reset();
		data->draw_list.Reset();
		vk_inst.current_frame = (vk_inst.current_frame + 1) % VulkanInstance::MAX_FRAMES_IN_FLIGHT;
		data->num_light_sources = 0;
	}

	TextureHandle_t CreateTexture(const CreateTextureArgs& args)
	{
		// NOTE: BPP are always 4 for now
		uint32_t bpp = 4;
		VkDeviceSize image_size = args.width * args.height * bpp;

		// Create staging buffer
		Vulkan::Buffer staging_buffer;
		Vulkan::CreateStagingBuffer(image_size, staging_buffer);

		// Copy data into the mapped memory of the staging buffer
		Vulkan::WriteBuffer(staging_buffer.ptr, (void*)args.pixels.data(), image_size);

		// Create texture image
		ResourceSlotmap<TextureResource>::ReservedResource reserved = data->texture_slotmap.Reserve();
		uint32_t num_mips = (uint32_t)std::floor(std::log2(std::max(args.width, args.height))) + 1;
		Vulkan::CreateImage(args.width, args.height, TEXTURE_FORMAT_TO_VK_FORMAT[args.format], VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, reserved.resource->image, num_mips);

		// Copy staging buffer data into final texture image memory (device local)
		Vulkan::TransitionImageLayoutImmediate(reserved.resource->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, num_mips);
		Vulkan::CopyBufferToImage(staging_buffer, reserved.resource->image, args.width, args.height);
		
		// Generate mips using blit
		Vulkan::GenerateMips(args.width, args.height, num_mips, TEXTURE_FORMAT_TO_VK_FORMAT[args.format], reserved.resource->image);

		// Clean up staging buffer
		Vulkan::DestroyBuffer(staging_buffer);

		// Create image view
		Vulkan::CreateImageView(VK_IMAGE_ASPECT_COLOR_BIT, reserved.resource->image, num_mips);

		// Allocate and update image descriptor
		reserved.resource->descriptor = data->descriptor_buffer_sampled_image.Allocate();
		reserved.resource->descriptor.WriteDescriptor(reserved.resource->image, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);

		return reserved.handle;
	}

	void DestroyTexture(TextureHandle_t handle)
	{
		data->texture_slotmap.Delete(handle);
	}

	MeshHandle_t CreateMesh(const CreateMeshArgs& args)
	{
		VkDeviceSize vertex_buffer_size = sizeof(Vertex) * args.vertices.size();
		VkDeviceSize index_buffer_size = sizeof(uint32_t) * args.indices.size();

		// Create staging buffer
		Vulkan::Buffer staging_buffer;
		Vulkan::CreateStagingBuffer(vertex_buffer_size + index_buffer_size, staging_buffer);

		// Write data into staging buffer
		Vulkan::WriteBuffer(staging_buffer.ptr, (void*)args.vertices.data(), vertex_buffer_size);
		Vulkan::WriteBuffer((uint8_t*)staging_buffer.ptr + vertex_buffer_size, (void*)args.indices.data(), index_buffer_size);

		// Reserve mesh resource
		ResourceSlotmap<MeshResource>::ReservedResource reserved = data->mesh_slotmap.Reserve();

		// Create vertex and index buffers
		Vulkan::CreateVertexBuffer(vertex_buffer_size, reserved.resource->vertex_buffer);
		Vulkan::CreateIndexBuffer(index_buffer_size, reserved.resource->index_buffer);

		// Copy staging buffer data into vertex and index buffers
		Vulkan::CopyBuffer(staging_buffer, reserved.resource->vertex_buffer, vertex_buffer_size);
		Vulkan::CopyBuffer(staging_buffer, reserved.resource->index_buffer, index_buffer_size, vertex_buffer_size);

		// Destroy staging buffer
		Vulkan::DestroyBuffer(staging_buffer);

		return reserved.handle;
	}

	void DestroyMesh(MeshHandle_t handle)
	{
		data->mesh_slotmap.Delete(handle);
	}

	MaterialHandle_t CreateMaterial(const CreateMaterialArgs& args)
	{
		ResourceSlotmap<MaterialResource>::ReservedResource reserved = data->material_slotmap.Reserve();

		// Base color factor and texture
		reserved.resource->data.base_color_factor = args.base_color_factor;
		if (VK_RESOURCE_HANDLE_VALID(args.base_color_texture_handle))
		{
			TextureResource* base_color_texture = data->texture_slotmap.Find(args.base_color_texture_handle);

			reserved.resource->base_color_texture_handle = args.base_color_texture_handle;
			reserved.resource->data.base_color_texture_index = base_color_texture->descriptor.GetIndex();
		}
		else
		{
			TextureResource* base_color_texture = data->texture_slotmap.Find(data->default_white_texture_handle);

			reserved.resource->base_color_texture_handle = data->default_white_texture_handle;
			reserved.resource->data.base_color_texture_index = base_color_texture->descriptor.GetIndex();
		}

		// Normal texture
		if (VK_RESOURCE_HANDLE_VALID(args.normal_texture_handle))
		{
			TextureResource* normal_texture = data->texture_slotmap.Find(args.normal_texture_handle);

			reserved.resource->normal_texture_handle = args.normal_texture_handle;
			reserved.resource->data.normal_texture_index = normal_texture->descriptor.GetIndex();
		}
		else
		{
			TextureResource* normal_texture = data->texture_slotmap.Find(data->default_normal_texture_handle);

			reserved.resource->normal_texture_handle = data->default_normal_texture_handle;
			reserved.resource->data.normal_texture_index = normal_texture->descriptor.GetIndex();
		}

		// Metallic roughness factors and texture
		reserved.resource->data.metallic_factor = args.metallic_factor;
		reserved.resource->data.roughness_factor = args.roughness_factor;
		if (VK_RESOURCE_HANDLE_VALID(args.metallic_roughness_texture_handle))
		{
			TextureResource* metallic_roughness_texture = data->texture_slotmap.Find(args.metallic_roughness_texture_handle);

			reserved.resource->metallic_roughness_texture_handle = args.metallic_roughness_texture_handle;
			reserved.resource->data.metallic_roughness_texture_index = metallic_roughness_texture->descriptor.GetIndex();
		}
		else
		{
			TextureResource* metallic_roughness_texture = data->texture_slotmap.Find(data->default_white_texture_handle);

			reserved.resource->metallic_roughness_texture_handle = data->default_white_texture_handle;
			reserved.resource->data.metallic_roughness_texture_index = metallic_roughness_texture->descriptor.GetIndex();
		}

		if (args.has_clearcoat)
		{
			reserved.resource->data.has_clearcoat = args.has_clearcoat;
			reserved.resource->data.clearcoat_alpha_factor = args.clearcoat_alpha_factor;
			reserved.resource->data.clearcoat_roughness_factor = args.clearcoat_roughness_factor;

			if (VK_RESOURCE_HANDLE_VALID(args.clearcoat_alpha_texture_handle))
			{
				TextureResource* clearcoat_alpha_texture = data->texture_slotmap.Find(args.clearcoat_alpha_texture_handle);

				reserved.resource->clearcoat_alpha_texture_handle = args.clearcoat_alpha_texture_handle;
				reserved.resource->data.clearcoat_alpha_texture_index = clearcoat_alpha_texture->descriptor.GetIndex();
			}
			else
			{
				TextureResource* clearcoat_alpha_texture = data->texture_slotmap.Find(data->default_white_texture_handle);

				reserved.resource->clearcoat_alpha_texture_handle = data->default_white_texture_handle;
				reserved.resource->data.clearcoat_alpha_texture_index = clearcoat_alpha_texture->descriptor.GetIndex();
			}

			if (VK_RESOURCE_HANDLE_VALID(args.clearcoat_normal_texture_handle))
			{
				TextureResource* clearcoat_normal_texture = data->texture_slotmap.Find(args.clearcoat_normal_texture_handle);

				reserved.resource->clearcoat_normal_texture_handle = args.clearcoat_normal_texture_handle;
				reserved.resource->data.clearcoat_normal_texture_index = clearcoat_normal_texture->descriptor.GetIndex();
			}
			else
			{
				TextureResource* clearcoat_normal_texture = data->texture_slotmap.Find(data->default_normal_texture_handle);

				reserved.resource->clearcoat_normal_texture_handle = data->default_normal_texture_handle;
				reserved.resource->data.clearcoat_normal_texture_index = clearcoat_normal_texture->descriptor.GetIndex();
			}

			if (VK_RESOURCE_HANDLE_VALID(args.clearcoat_roughness_texture_handle))
			{
				TextureResource* clearcoat_roughness_texture = data->texture_slotmap.Find(args.clearcoat_roughness_texture_handle);

				reserved.resource->clearcoat_roughness_texture_handle = args.clearcoat_roughness_texture_handle;
				reserved.resource->data.clearcoat_roughness_texture_index = clearcoat_roughness_texture->descriptor.GetIndex();
			}
			else
			{
				TextureResource* clearcoat_roughness_texture = data->texture_slotmap.Find(data->default_white_texture_handle);

				reserved.resource->clearcoat_roughness_texture_handle = data->default_white_texture_handle;
				reserved.resource->data.clearcoat_roughness_texture_index = clearcoat_roughness_texture->descriptor.GetIndex();
			}
		}

		// Samplers
		// NOTE: Currently this is always the default sampler, ideally the model loading also creates samplers
		reserved.resource->data.sampler_index = 0;

		VkDeviceSize material_size = sizeof(MaterialData);

		// TODO: Suballocate from big upload buffer, having a staging buffer for every little upload is unnecessary
		// Create staging buffer
		Vulkan::Buffer staging_buffer;
		Vulkan::CreateStagingBuffer(material_size, staging_buffer);

		// Write data into staging buffer
		Vulkan::WriteBuffer(staging_buffer.ptr, (void*)&reserved.resource->data, material_size);

		// Copy staging buffer material resource into device local material buffer
		Vulkan::CopyBuffer(staging_buffer, data->material_buffer, material_size, 0, reserved.handle.index * material_size);

		// Destroy staging buffer
		Vulkan::DestroyBuffer(staging_buffer);

		return reserved.handle;
	}

	void DestroyMaterial(MaterialHandle_t handle)
	{
		data->material_slotmap.Delete(handle);
		// NOTE: We could update the material buffer entry here to be 0 or some invalid value,
		// but it will be overwritten anyways when the slot is re-used
	}

	void SubmitMesh(MeshHandle_t mesh_handle, MaterialHandle_t material_handle, const glm::mat4& transform)
	{
		Vulkan::Buffer& instance_buffer = data->instance_buffers[vk_inst.current_frame];
		memcpy((glm::mat4*)instance_buffer.ptr + data->draw_list.current_entry, &transform, sizeof(glm::mat4));

		DrawList::Entry& entry = data->draw_list.GetNextEntry();
		entry.mesh_handle = mesh_handle;
		entry.material_handle = material_handle;
		entry.transform = transform;
	}

	void SubmitPointlight(const glm::vec3& pos, const glm::vec3& color, float intensity)
	{
		VK_ASSERT(data->num_light_sources < MAX_LIGHT_SOURCES && "Exceeded the maximum amount of light sources");

		PointlightData* pointlight_data = (PointlightData*)data->light_uniform_buffers[vk_inst.current_frame].ptr;
		pointlight_data[data->num_light_sources].position = pos;
		pointlight_data[data->num_light_sources].intensity = intensity;
		pointlight_data[data->num_light_sources].color = color;

		data->num_light_sources++;
	}

}
