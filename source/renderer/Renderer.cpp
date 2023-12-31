#include "renderer/Renderer.h"
#include "renderer/VulkanBackend.h"
#include "renderer/DescriptorBuffer.h"
#include "renderer/ResourceSlotmap.h"
#include "renderer/RenderPass.h"
#include "renderer/Texture.h"
#include "renderer/Buffer.h"
#include "Common.h"
#include "Shared.glsl.h"
#include "Assets.h"

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

#define BEGIN_PASS(cmd_buffer, pass, begin_info) pass.Begin(cmd_buffer, begin_info)
#define END_PASS(cmd_buffer, pass) pass.End(cmd_buffer)

	static constexpr uint32_t MAX_DRAW_LIST_ENTRIES = 10000;
	static constexpr uint32_t IBL_HDR_CUBEMAP_RESOLUTION = 1024;
	static constexpr uint32_t IBL_IRRADIANCE_CUBEMAP_RESOLUTION = 64;
	static constexpr uint32_t IBL_PREFILTERED_CUBEMAP_RESOLUTION = 1024;
	static constexpr uint32_t IBL_PREFILTERED_CUBEMAP_NUM_SAMPLES = 1024;
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

	struct Mesh
	{
		Buffer vertex_buffer;
		Buffer index_buffer;
	};

	struct Data
	{
		::GLFWwindow* window = nullptr;

		// Resource slotmaps
		ResourceSlotmap<Texture> texture_slotmap;
		ResourceSlotmap<Mesh> mesh_slotmap;

		// Command buffers and synchronization primitives
		std::vector<VkCommandBuffer> command_buffers;
		std::vector<VkSemaphore> render_finished_semaphores;
		std::vector<VkFence> in_flight_fences;

		// Descriptor buffers
		DescriptorBuffer descriptor_buffer_uniform{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, (uint32_t)vk_inst.descriptor_sizes.uniform_buffer, RESERVED_DESCRIPTOR_UBO_COUNT * VulkanInstance::MAX_FRAMES_IN_FLIGHT };
		DescriptorBuffer descriptor_buffer_storage{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, (uint32_t)vk_inst.descriptor_sizes.storage_buffer };
		DescriptorBuffer descriptor_buffer_storage_image{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, (uint32_t)vk_inst.descriptor_sizes.storage_image };
		DescriptorBuffer descriptor_buffer_sampled_image{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, (uint32_t)vk_inst.descriptor_sizes.sampled_image };
		DescriptorBuffer descriptor_buffer_sampler{ VK_DESCRIPTOR_TYPE_SAMPLER, (uint32_t)vk_inst.descriptor_sizes.sampler };

		std::array<VkDescriptorBufferBindingInfoEXT, 5> descriptor_buffer_binding_infos;
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
			TextureHandle_t hdr_handle;
			Texture* hdr = nullptr;
			TextureHandle_t depth_handle;
			Texture* depth = nullptr;
			TextureHandle_t sdr_handle;
			Texture* sdr = nullptr;
		} render_targets;

		struct IBL
		{
			TextureHandle_t brdf_lut_handle;
			Texture* brdf_lut = nullptr;
		} ibl;

		struct UBOs
		{
			std::vector<Vulkan::Buffer> camera_buffers;
			std::vector<Vulkan::Buffer> light_buffers;
			std::vector<Vulkan::Buffer> material_buffers;
			std::vector<Vulkan::Buffer> settings_buffers;
		} ubos;
		
		struct Frame
		{
			// TODO: Free reserved descriptors on Exit()
			DescriptorAllocation ubo_descriptors;
		} per_frame[VulkanInstance::MAX_FRAMES_IN_FLIGHT];

		// TODO: Free reserved descriptors on Exit()
		DescriptorAllocation reserved_storage_image_descriptors;

		// Draw submission list
		DrawList draw_list;
		uint32_t num_pointlights;

		std::vector<Vulkan::Buffer> instance_buffers;

		// Default resources
		TextureHandle_t default_white_texture_handle;
		TextureResource* default_white_texture;
		TextureHandle_t default_normal_texture_handle;
		TextureResource* default_normal_texture;

		VkSampler default_sampler = VK_NULL_HANDLE;
		Buffer unit_cube_vb;
		Buffer unit_cube_ib;

		TextureHandle_t skybox_texture_handle;
		// TODO: Free reserved descriptors on Exit()
		DescriptorAllocation reserved_sampler_descriptors;

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
		sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler_info.anisotropyEnable = VK_TRUE;
		sampler_info.maxAnisotropy = vk_inst.device_props.max_anisotropy;
		sampler_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		sampler_info.unnormalizedCoordinates = VK_FALSE;
		sampler_info.compareEnable = VK_FALSE;
		sampler_info.compareOp = VK_COMPARE_OP_NEVER;
		sampler_info.mipLodBias = 0.0f;
		sampler_info.minLod = 0.0f;
		//sampler_info.maxLod = (float)num_mips;
		sampler_info.maxLod = std::numeric_limits<float>::max();
		sampler_info.flags = 0;

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
		texture_args.src_stride = 4;
		texture_args.pixels = white_pixel;

		data->default_white_texture_handle = CreateTexture(texture_args);
		data->default_white_texture = data->texture_slotmap.Find(data->default_white_texture_handle);

		// Default normal texture
		std::vector<uint8_t> normal_pixel = { 127, 127, 255, 255 };
		texture_args.pixels = normal_pixel;

		data->default_normal_texture_handle = CreateTexture(texture_args);
		data->default_normal_texture = data->texture_slotmap.Find(data->default_normal_texture_handle);
	}

	static void CreateUnitCubeBuffers()
	{
		// Calculate the vertex and index buffer size
		VkDeviceSize vb_size = UNIT_CUBE_VERTICES.size() * sizeof(glm::vec3);
		VkDeviceSize ib_size = UNIT_CUBE_INDICES.size() * sizeof(uint16_t);

		// Create the staging buffer
		Vulkan::Buffer staging_buffer;
		Vulkan::CreateStagingBuffer(vb_size + ib_size, staging_buffer);

		// Write data to the staging buffer
		Vulkan::WriteBuffer(staging_buffer.ptr, (void*)&UNIT_CUBE_VERTICES[0], vb_size);
		Vulkan::WriteBuffer(staging_buffer.ptr + vb_size, (void*)&UNIT_CUBE_INDICES[0], ib_size);

		// Create cube vertex and index buffer
		Vulkan::CreateVertexBuffer(vb_size, data->unit_cube_vertex_buffer);
		Vulkan::CreateIndexBuffer(ib_size, data->unit_cube_index_buffer);

		// Copy staged vertex and index data to the device local buffers
		Vulkan::CopyBuffer(staging_buffer, data->unit_cube_vertex_buffer, vb_size, 0, 0);
		Vulkan::CopyBuffer(staging_buffer, data->unit_cube_index_buffer, ib_size, vb_size, 0);

		// Clean up, destroy the staging buffer
		Vulkan::DestroyBuffer(staging_buffer);
	}

	static void CreateUniformBuffers()
	{
		data->ubos.settings_buffers.resize(VulkanInstance::MAX_FRAMES_IN_FLIGHT);
		VkDeviceSize settings_buffer_size = sizeof(RenderSettings);

		data->ubos.camera_buffers.resize(VulkanInstance::MAX_FRAMES_IN_FLIGHT);
		VkDeviceSize camera_buffer_size = sizeof(CameraData);

		data->ubos.light_buffers.resize(VulkanInstance::MAX_FRAMES_IN_FLIGHT);
		VkDeviceSize light_buffer_size = sizeof(uint32_t) + MAX_LIGHT_SOURCES * sizeof(PointlightData);

		data->ubos.material_buffers.resize(VulkanInstance::MAX_FRAMES_IN_FLIGHT);
		VkDeviceSize material_buffer_size = MAX_UNIQUE_MATERIALS * sizeof(MaterialData);

		for (size_t i = 0; i < VulkanInstance::MAX_FRAMES_IN_FLIGHT; ++i)
		{
			data->per_frame[i].ubo_descriptors = data->descriptor_buffer_uniform.Allocate(RESERVED_DESCRIPTOR_UBO_COUNT, vk_inst.device_props.descriptor_buffer_offset_alignment);

			Vulkan::CreateUniformBuffer(settings_buffer_size, data->ubos.settings_buffers[i]);
			data->per_frame[i].ubo_descriptors.WriteDescriptor(data->ubos.settings_buffers[i],
				settings_buffer_size, RESERVED_DESCRIPTOR_UBO_SETTINGS);

			Vulkan::CreateUniformBuffer(camera_buffer_size, data->ubos.camera_buffers[i]);
			data->per_frame[i].ubo_descriptors.WriteDescriptor(data->ubos.camera_buffers[i],
				camera_buffer_size, RESERVED_DESCRIPTOR_UBO_CAMERA);

			Vulkan::CreateUniformBuffer(light_buffer_size, data->ubos.light_buffers[i]);
			data->per_frame[i].ubo_descriptors.WriteDescriptor(data->ubos.light_buffers[i],
				light_buffer_size, RESERVED_DESCRIPTOR_UBO_LIGHTS);

			Vulkan::CreateUniformBuffer(material_buffer_size, data->ubos.material_buffers[i]);
			data->per_frame[i].ubo_descriptors.WriteDescriptor(data->ubos.material_buffers[i],
				material_buffer_size, RESERVED_DESCRIPTOR_UBO_MATERIALS);
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
			VkCheckResult(vkMapMemory(vk_inst.device, instance_buffer.memory, 0, instance_buffer_size, 0, (void**)&instance_buffer.ptr));
		}
	}

	static void CreateRenderTargets()
	{
		// Create HDR render target
		{
			if (!data->render_targets.hdr)
			{
				ResourceSlotmap<TextureResource>::ReservedResource reserved = data->texture_slotmap.Reserve();
				data->render_targets.hdr_handle = reserved.handle;
				data->render_targets.hdr = reserved.resource;
			}

			Vulkan::CreateImage(
				vk_inst.swapchain.extent.width, vk_inst.swapchain.extent.height,
				VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				data->render_targets.hdr->image
			);
			Vulkan::CreateImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT, &data->render_targets.hdr->image, data->render_targets.hdr->view);
			data->reserved_storage_image_descriptors.WriteDescriptor(data->render_targets.hdr->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, RESERVED_DESCRIPTOR_STORAGE_IMAGE_HDR);

			data->render_passes.skybox.SetAttachment(RenderPass::ATTACHMENT_SLOT_COLOR0, &data->render_targets.hdr->view);
			data->render_passes.lighting.SetAttachment(RenderPass::ATTACHMENT_SLOT_COLOR0, &data->render_targets.hdr->view);
			data->render_passes.post_process.SetAttachment(RenderPass::ATTACHMENT_SLOT_READ_ONLY0, &data->render_targets.hdr->view);
		}

		// Create depth render target
		{
			if (!data->render_targets.depth)
			{
				ResourceSlotmap<TextureResource>::ReservedResource reserved = data->texture_slotmap.Reserve();
				data->render_targets.depth_handle = reserved.handle;
				data->render_targets.depth = reserved.resource;
			}

			Vulkan::CreateImage(
				vk_inst.swapchain.extent.width, vk_inst.swapchain.extent.height,
				Vulkan::FindDepthFormat(),
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				data->render_targets.depth->image
			);
			Vulkan::CreateImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_DEPTH_BIT, &data->render_targets.depth->image, data->render_targets.depth->view);

			data->render_passes.skybox.SetAttachment(RenderPass::ATTACHMENT_SLOT_DEPTH_STENCIL, &data->render_targets.depth->view);
			data->render_passes.lighting.SetAttachment(RenderPass::ATTACHMENT_SLOT_DEPTH_STENCIL, &data->render_targets.depth->view);
		}

		// Create SDR render target
		{
			if (!data->render_targets.sdr)
			{
				ResourceSlotmap<TextureResource>::ReservedResource reserved = data->texture_slotmap.Reserve();
				data->render_targets.sdr_handle = reserved.handle;
				data->render_targets.sdr = reserved.resource;
			}

			Vulkan::CreateImage(
				vk_inst.swapchain.extent.width, vk_inst.swapchain.extent.height,
				vk_inst.swapchain.format,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				data->render_targets.sdr->image
			);
			Vulkan::CreateImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT, &data->render_targets.sdr->image, data->render_targets.sdr->view);
			data->reserved_storage_image_descriptors.WriteDescriptor(data->render_targets.sdr->view, VK_IMAGE_LAYOUT_GENERAL, RESERVED_DESCRIPTOR_STORAGE_IMAGE_SDR);

			data->render_passes.post_process.SetAttachment(RenderPass::ATTACHMENT_SLOT_READ_WRITE0, &data->render_targets.sdr->view);
			data->imgui.render_pass.SetAttachment(RenderPass::ATTACHMENT_SLOT_COLOR0, &data->render_targets.sdr->view);
		}
	}

	static void DestroyRenderTargets()
	{
		Vulkan::DestroyImageView(data->render_targets.hdr->view);
		Vulkan::DestroyImage(data->render_targets.hdr->image);

		Vulkan::DestroyImageView(data->render_targets.depth->view);
		Vulkan::DestroyImage(data->render_targets.depth->image);

		Vulkan::DestroyImageView(data->render_targets.sdr->view);
		Vulkan::DestroyImage(data->render_targets.sdr->image);
	}

	static void CreateRenderPasses()
	{
		data->reserved_storage_image_descriptors = data->descriptor_buffer_storage_image.Allocate(RESERVED_DESCRIPTOR_STORAGE_IMAGE_COUNT);

		std::vector<VkDescriptorSetLayout> descriptor_set_layouts =
		{
			data->descriptor_buffer_uniform.GetDescriptorSetLayout(),
			data->descriptor_buffer_storage.GetDescriptorSetLayout(),
			data->descriptor_buffer_storage_image.GetDescriptorSetLayout(),
			data->descriptor_buffer_sampled_image.GetDescriptorSetLayout(),
			data->descriptor_buffer_sampler.GetDescriptorSetLayout()
		};

		// Skybox raster pass
		{
			std::vector<RenderPass::AttachmentInfo> attachment_infos(2);
			attachment_infos[0].attachment_slot = RenderPass::ATTACHMENT_SLOT_COLOR0;
			attachment_infos[0].format = VK_FORMAT_R16G16B16A16_SFLOAT;
			attachment_infos[0].expected_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			attachment_infos[0].load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachment_infos[0].store_op = VK_ATTACHMENT_STORE_OP_STORE;
			attachment_infos[0].clear_value.color = { 0.0f, 0.0f, 0.0f, 1.0f };

			attachment_infos[1].attachment_slot = RenderPass::ATTACHMENT_SLOT_DEPTH_STENCIL;
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

			data->render_passes.skybox.Build(descriptor_set_layouts, push_ranges, info);
		}

		// Lighting raster pass
		{
			std::vector<RenderPass::AttachmentInfo> attachment_infos(2);
			attachment_infos[0].attachment_slot = RenderPass::ATTACHMENT_SLOT_COLOR0;
			attachment_infos[0].format = VK_FORMAT_R16G16B16A16_SFLOAT;
			attachment_infos[0].expected_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			attachment_infos[0].load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
			attachment_infos[0].store_op = VK_ATTACHMENT_STORE_OP_STORE;

			attachment_infos[1].attachment_slot = RenderPass::ATTACHMENT_SLOT_DEPTH_STENCIL;
			attachment_infos[1].format = VK_FORMAT_D32_SFLOAT;
			attachment_infos[1].expected_layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
			attachment_infos[1].load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
			attachment_infos[1].store_op = VK_ATTACHMENT_STORE_OP_STORE;

			data->render_passes.lighting.SetAttachmentInfos(attachment_infos);

			std::vector<VkPushConstantRange> push_ranges(1);
			push_ranges[0].size = 5 * sizeof(uint32_t);
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

			data->render_passes.lighting.Build(descriptor_set_layouts, push_ranges, info);
		}

		// Post-processing compute pass
		{
			std::vector<RenderPass::AttachmentInfo> attachment_infos(2);
			attachment_infos[0].attachment_slot = RenderPass::ATTACHMENT_SLOT_READ_ONLY0;
			attachment_infos[0].format = VK_FORMAT_R16G16B16A16_SFLOAT;
			attachment_infos[0].expected_layout = VK_IMAGE_LAYOUT_GENERAL;

			attachment_infos[1].attachment_slot = RenderPass::ATTACHMENT_SLOT_READ_WRITE0;
			attachment_infos[1].format = vk_inst.swapchain.format;
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

			data->render_passes.post_process.Build(descriptor_set_layouts, push_ranges, info);
		}

		// Dear ImGui render pass
		{
			std::vector<RenderPass::AttachmentInfo> attachment_infos(1);
			attachment_infos[0].attachment_slot = RenderPass::ATTACHMENT_SLOT_COLOR0;
			attachment_infos[0].format = vk_inst.swapchain.format;
			attachment_infos[0].expected_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			attachment_infos[0].load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
			attachment_infos[0].store_op = VK_ATTACHMENT_STORE_OP_STORE;

			data->imgui.render_pass.SetAttachmentInfos(attachment_infos);
		}

		// Generate Cubemap from Equirectangular Map pass
		{
			std::vector<RenderPass::AttachmentInfo> attachment_infos(1);
			attachment_infos[0].attachment_slot = RenderPass::ATTACHMENT_SLOT_COLOR0;
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

			data->render_passes.gen_cubemap.Build(descriptor_set_layouts, push_ranges, info);
		}

		// Generate Irradiance Cube pass
		{
			std::vector<RenderPass::AttachmentInfo> attachment_infos(1);
			attachment_infos[0].attachment_slot = RenderPass::ATTACHMENT_SLOT_COLOR0;
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

			data->render_passes.gen_irradiance_cube.Build(descriptor_set_layouts, push_ranges, info);
		}

		// Generate Prefiltered Cube pass
		{
			std::vector<RenderPass::AttachmentInfo> attachment_infos(1);
			attachment_infos[0].attachment_slot = RenderPass::ATTACHMENT_SLOT_COLOR0;
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

			data->render_passes.gen_prefiltered_cube.Build(descriptor_set_layouts, push_ranges, info);
		}

		// Generate BRDF LUT pass
		{
			std::vector<RenderPass::AttachmentInfo> attachment_infos(1);
			attachment_infos[0].attachment_slot = RenderPass::ATTACHMENT_SLOT_COLOR0;
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

			data->render_passes.gen_brdf_lut.Build(descriptor_set_layouts, push_ranges, info);
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
		init_info.ColorAttachmentFormat = data->render_targets.sdr->image.format;
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

	static void GenerateCubeMapFromEquirectangular(uint32_t src_texture_index, uint32_t src_sampler_index, Vulkan::Image& cubemap_image, Vulkan::ImageView& cubemap_view)
	{
		// Create hdr environment cubemap
		uint32_t num_cube_mips = (uint32_t)std::floor(std::log2(std::max(IBL_HDR_CUBEMAP_RESOLUTION, IBL_HDR_CUBEMAP_RESOLUTION))) + 1;
		Vulkan::CreateImageCube(IBL_HDR_CUBEMAP_RESOLUTION, IBL_HDR_CUBEMAP_RESOLUTION, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, cubemap_image, num_cube_mips);
		Vulkan::CreateImageView(VK_IMAGE_VIEW_TYPE_CUBE, VK_IMAGE_ASPECT_COLOR_BIT, &cubemap_image, cubemap_view, 0, num_cube_mips, 0, 6);

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
		std::vector<Vulkan::ImageView> attachment_image_views;

		for (uint32_t mip = 0; mip < num_cube_mips; ++mip)
		{
			for (uint32_t face = 0; face < 6; ++face)
			{
				// Render current face to the offscreen render target
				RenderPass::BeginInfo begin_info = {};
				begin_info.render_width = static_cast<float>(IBL_HDR_CUBEMAP_RESOLUTION) * std::pow(0.5f, mip);
				begin_info.render_height = static_cast<float>(IBL_HDR_CUBEMAP_RESOLUTION) * std::pow(0.5f, mip);

				Vulkan::ImageView& attachment_view = attachment_image_views.emplace_back();
				Vulkan::CreateImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT, &cubemap_image, attachment_view, mip, 1, face, 1);
				data->render_passes.gen_cubemap.SetAttachment(RenderPass::ATTACHMENT_SLOT_COLOR0, &attachment_view);

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

					vk_inst.pFunc.cmd_bind_descriptor_buffers_ext(command_buffer, (uint32_t)data->descriptor_buffer_binding_infos.size(), data->descriptor_buffer_binding_infos.data());
					data->render_passes.gen_cubemap.SetDescriptorBufferOffsets(command_buffer, 0, 5, &data->descriptor_buffer_indices[0], &data->descriptor_buffer_offsets[0]);

					VkDeviceSize vb_offset = 0;
					vkCmdBindVertexBuffers(command_buffer, 0, 1, &data->unit_cube_vertex_buffer.buffer, &vb_offset);
					vkCmdBindIndexBuffer(command_buffer, data->unit_cube_index_buffer.buffer, 0, VK_INDEX_TYPE_UINT16);
					vkCmdDrawIndexed(command_buffer, data->unit_cube_index_buffer.size / sizeof(uint16_t), 1, 0, 0, 0);
				}
				END_PASS(command_buffer, data->render_passes.gen_cubemap);
			}
		}

		Vulkan::CmdTransitionImageLayout(command_buffer,
			cubemap_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		);
		Vulkan::EndImmediateCommand(command_buffer);

		for (auto& attachment_image_view : attachment_image_views)
		{
			Vulkan::DestroyImageView(attachment_image_view);
		}
	}

	static void GenerateIrradianceCube(uint32_t src_texture_index, uint32_t src_sampler_index, Vulkan::Image& cubemap_image, Vulkan::ImageView& cubemap_view)
	{
		// Create irradiance cubemap
		uint32_t num_cube_mips = (uint32_t)std::floor(std::log2(std::max(IBL_IRRADIANCE_CUBEMAP_RESOLUTION, IBL_IRRADIANCE_CUBEMAP_RESOLUTION))) + 1;
		Vulkan::CreateImageCube(IBL_IRRADIANCE_CUBEMAP_RESOLUTION, IBL_IRRADIANCE_CUBEMAP_RESOLUTION, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, cubemap_image, num_cube_mips);
		Vulkan::CreateImageView(VK_IMAGE_VIEW_TYPE_CUBE, VK_IMAGE_ASPECT_COLOR_BIT, &cubemap_image, cubemap_view, 0, num_cube_mips, 0, 6);

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

		push_consts.src_texture_index = src_texture_index;
		push_consts.src_sampler_index = src_sampler_index;

		VkCommandBuffer command_buffer = Vulkan::BeginImmediateCommand();
		std::vector<Vulkan::ImageView> attachment_image_views;

		for (uint32_t mip = 0; mip < num_cube_mips; ++mip)
		{
			// Render current face to the offscreen render target
			RenderPass::BeginInfo begin_info = {};
			begin_info.render_width = static_cast<float>(IBL_IRRADIANCE_CUBEMAP_RESOLUTION) * std::pow(0.5f, mip);
			begin_info.render_height = static_cast<float>(IBL_IRRADIANCE_CUBEMAP_RESOLUTION) * std::pow(0.5f, mip);

			for (uint32_t face = 0; face < 6; ++face)
			{
				Vulkan::ImageView& attachment_view = attachment_image_views.emplace_back();
				Vulkan::CreateImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT, &cubemap_image, attachment_view, mip, 1, face, 1);
				data->render_passes.gen_irradiance_cube.SetAttachment(RenderPass::ATTACHMENT_SLOT_COLOR0, &attachment_view);

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

					vk_inst.pFunc.cmd_bind_descriptor_buffers_ext(command_buffer, (uint32_t)data->descriptor_buffer_binding_infos.size(), data->descriptor_buffer_binding_infos.data());
					data->render_passes.gen_irradiance_cube.SetDescriptorBufferOffsets(command_buffer, 0, 5, &data->descriptor_buffer_indices[0], &data->descriptor_buffer_offsets[0]);

					VkDeviceSize vb_offset = 0;
					vkCmdBindVertexBuffers(command_buffer, 0, 1, &data->unit_cube_vertex_buffer.buffer, &vb_offset);
					vkCmdBindIndexBuffer(command_buffer, data->unit_cube_index_buffer.buffer, 0, VK_INDEX_TYPE_UINT16);
					vkCmdDrawIndexed(command_buffer, data->unit_cube_index_buffer.size / sizeof(uint16_t), 1, 0, 0, 0);
				}
				END_PASS(command_buffer, data->render_passes.gen_irradiance_cube);
			}
		}

		Vulkan::CmdTransitionImageLayout(command_buffer,
			cubemap_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		);
		Vulkan::EndImmediateCommand(command_buffer);

		for (auto& attachment_image_view : attachment_image_views)
		{
			Vulkan::DestroyImageView(attachment_image_view);
		}
	}

	static void GeneratePrefilteredEnvMap(uint32_t src_texture_index, uint32_t src_sampler_index, Vulkan::Image& cubemap_image, Vulkan::ImageView& cubemap_view)
	{
		// Create prefiltered cubemap
		uint32_t num_cube_mips = (uint32_t)std::floor(std::log2(std::max(IBL_PREFILTERED_CUBEMAP_RESOLUTION, IBL_PREFILTERED_CUBEMAP_RESOLUTION))) + 1;
		Vulkan::CreateImageCube(IBL_PREFILTERED_CUBEMAP_RESOLUTION, IBL_PREFILTERED_CUBEMAP_RESOLUTION, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, cubemap_image, num_cube_mips);
		Vulkan::CreateImageView(VK_IMAGE_VIEW_TYPE_CUBE, VK_IMAGE_ASPECT_COLOR_BIT, &cubemap_image, cubemap_view, 0, num_cube_mips, 0, 6);

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
		std::vector<Vulkan::ImageView> attachment_image_views;

		for (uint32_t mip = 0; mip < num_cube_mips; ++mip)
		{
			RenderPass::BeginInfo begin_info = {};
			begin_info.render_width = static_cast<float>(IBL_PREFILTERED_CUBEMAP_RESOLUTION) * std::pow(0.5f, mip);
			begin_info.render_height = static_cast<float>(IBL_PREFILTERED_CUBEMAP_RESOLUTION) * std::pow(0.5f, mip);

			for (uint32_t face = 0; face < 6; ++face)
			{
				Vulkan::ImageView& attachment_view = attachment_image_views.emplace_back();
				Vulkan::CreateImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT, &cubemap_image, attachment_view, mip, 1, face, 1);
				data->render_passes.gen_prefiltered_cube.SetAttachment(RenderPass::ATTACHMENT_SLOT_COLOR0, &attachment_view);

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

					vk_inst.pFunc.cmd_bind_descriptor_buffers_ext(command_buffer, (uint32_t)data->descriptor_buffer_binding_infos.size(), data->descriptor_buffer_binding_infos.data());
					data->render_passes.gen_prefiltered_cube.SetDescriptorBufferOffsets(command_buffer, 0, 5, &data->descriptor_buffer_indices[0], &data->descriptor_buffer_offsets[0]);

					VkDeviceSize vb_offset = 0;
					vkCmdBindVertexBuffers(command_buffer, 0, 1, &data->unit_cube_vertex_buffer.buffer, &vb_offset);
					vkCmdBindIndexBuffer(command_buffer, data->unit_cube_index_buffer.buffer, 0, VK_INDEX_TYPE_UINT16);
					vkCmdDrawIndexed(command_buffer, data->unit_cube_index_buffer.size / sizeof(uint16_t), 1, 0, 0, 0);
				}
				END_PASS(command_buffer, data->render_passes.gen_prefiltered_cube);
			}
		}

		Vulkan::CmdTransitionImageLayout(command_buffer,
			cubemap_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		);
		Vulkan::EndImmediateCommand(command_buffer);

		for (auto& attachment_image_view : attachment_image_views)
		{
			Vulkan::DestroyImageView(attachment_image_view);
		}
	}

	static void GenerateBRDF_LUT()
	{
		ResourceSlotmap<TextureResource>::ReservedResource reserved_brdf_lut = data->texture_slotmap.Reserve();
		data->ibl.brdf_lut_handle = reserved_brdf_lut.handle;
		data->ibl.brdf_lut = reserved_brdf_lut.resource;

		// Create the BRDF LUT
		Vulkan::CreateImage(IBL_BRDF_LUT_RESOLUTION, IBL_BRDF_LUT_RESOLUTION, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, data->ibl.brdf_lut->image);
		Vulkan::CreateImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT, &data->ibl.brdf_lut->image, data->ibl.brdf_lut->view);

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

		data->render_passes.gen_brdf_lut.SetAttachment(RenderPass::ATTACHMENT_SLOT_COLOR0, &data->ibl.brdf_lut->view);

		BEGIN_PASS(command_buffer, data->render_passes.gen_brdf_lut, begin_info);
		{
			viewport.width = begin_info.render_width;
			viewport.height = begin_info.render_height;

			scissor_rect.extent.width = viewport.width;
			scissor_rect.extent.height = viewport.height;

			vkCmdSetViewport(command_buffer, 0, 1, &viewport);
			vkCmdSetScissor(command_buffer, 0, 1, &scissor_rect);

			data->render_passes.gen_brdf_lut.PushConstants(command_buffer, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(uint32_t), &push_consts);

			vk_inst.pFunc.cmd_bind_descriptor_buffers_ext(command_buffer, (uint32_t)data->descriptor_buffer_binding_infos.size(), data->descriptor_buffer_binding_infos.data());
			data->render_passes.gen_brdf_lut.SetDescriptorBufferOffsets(command_buffer, 0, 5, &data->descriptor_buffer_indices[0], &data->descriptor_buffer_offsets[0]);

			vkCmdDraw(command_buffer, 3, 1, 0, 0);
		}
		END_PASS(command_buffer, data->render_passes.gen_brdf_lut);

		Vulkan::CmdTransitionImageLayout(command_buffer,
			data->ibl.brdf_lut->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		);
		Vulkan::EndImmediateCommand(command_buffer);

		data->ibl.brdf_lut->descriptor = data->descriptor_buffer_sampled_image.Allocate();
		data->ibl.brdf_lut->descriptor.WriteDescriptor(data->ibl.brdf_lut->view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
	}

	void Init(::GLFWwindow* window)
	{
		Vulkan::Init(window);

		data = new Data();
		data->window = window;

		data->descriptor_buffer_binding_infos = std::array<VkDescriptorBufferBindingInfoEXT, 5>
		{
			data->descriptor_buffer_uniform.GetBindingInfo(),
			data->descriptor_buffer_storage.GetBindingInfo(),
			data->descriptor_buffer_storage_image.GetBindingInfo(),
			data->descriptor_buffer_sampled_image.GetBindingInfo(),
			data->descriptor_buffer_sampler.GetBindingInfo()
		};

		CreateRenderPasses();
		CreateRenderTargets();
		InitDearImGui();

		CreateCommandBuffers();
		CreateSyncObjects();

		CreateUniformBuffers();
		CreateInstanceBuffers();

		CreateDefaultSamplers();
		CreateDefaultTextures();
		CreateUnitCubeBuffers();
		GenerateBRDF_LUT();

		// Set default render settings
		data->settings.use_direct_light = true;
		data->settings.use_squared_roughness = true;
		data->settings.use_clearcoat = true;
		data->settings.use_ibl = true;
		data->settings.use_clearcoat_specular_ibl = true;

		data->settings.exposure = 1.5f;
		data->settings.gamma = 2.4f;

		data->settings.debug_render_mode = DEBUG_RENDER_MODE_NONE;
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
			Vulkan::DestroyBuffer(data->ubos.camera_buffers[i]);
			Vulkan::DestroyBuffer(data->ubos.light_buffers[i]);
			Vulkan::DestroyBuffer(data->ubos.material_buffers[i]);
			Vulkan::DestroyBuffer(data->ubos.settings_buffers[i]);
			Vulkan::DestroyBuffer(data->instance_buffers[i]);
		}

		// Clean up the renderer data
		delete data;

		// Finally, exit the vulkan render backend
		Vulkan::Exit();
	}

	void BeginFrame(const BeginFrameInfo& frame_info)
	{
		// Wait for completion of all rendering for the current swapchain image
		VkCheckResult(vkWaitForFences(vk_inst.device, 1, &data->in_flight_fences[vk_inst.current_frame], VK_TRUE, UINT64_MAX));

		// Reset the fence
		VkCheckResult(vkResetFences(vk_inst.device, 1, &data->in_flight_fences[vk_inst.current_frame]));

		// Get an available image index from the swap chain
		VkResult result = Vulkan::SwapChainAcquireNextImage();
		if ((result == VK_ERROR_OUT_OF_DATE_KHR) || (result == VK_SUBOPTIMAL_KHR))
		{
			if (result == VK_ERROR_OUT_OF_DATE_KHR)
			{
				Vulkan::RecreateSwapChain();
				DestroyRenderTargets();
				CreateRenderTargets();
			}

			return;
		}
		else
		{
			VkCheckResult(result);
		}

		// Reset and record the command buffer
		VkCommandBuffer graphics_command_buffer = data->command_buffers[vk_inst.current_frame];
		vkResetCommandBuffer(graphics_command_buffer, 0);

		ImGui_ImplGlfw_NewFrame();
		ImGui_ImplVulkan_NewFrame();
		ImGui::NewFrame();

		// Set UBO data for the current frame, like camera data and settings
		CameraData camera_data = {};
		camera_data.view = frame_info.view;
		camera_data.proj = frame_info.proj;
		camera_data.view_pos = glm::inverse(frame_info.view)[3];
		memcpy(data->ubos.camera_buffers[vk_inst.current_frame].ptr, &camera_data, sizeof(camera_data));

		memcpy(data->ubos.settings_buffers[vk_inst.current_frame].ptr, &data->settings, sizeof(RenderSettings));

		data->skybox_texture_handle = frame_info.skybox_texture_handle;
	}

	void RenderFrame()
	{
		// Update number of lights in the light ubo
		memcpy((PointlightData*)data->ubos.light_buffers[vk_inst.current_frame].ptr + MAX_LIGHT_SOURCES, &data->num_pointlights, sizeof(uint32_t));

		// Update UBO descriptor buffer offset
		// We need a UBO per in-flight frame, so we need to update the offset at which we want to bind our UBOs from the descriptor buffer
		data->descriptor_buffer_offsets[DESCRIPTOR_SET_UBO] = VK_ALIGN_POW2(RESERVED_DESCRIPTOR_UBO_COUNT * vk_inst.current_frame * vk_inst.descriptor_sizes.uniform_buffer, vk_inst.device_props.descriptor_buffer_offset_alignment);

		VkCommandBuffer command_buffer = data->command_buffers[vk_inst.current_frame];

		VkCommandBufferBeginInfo command_buffer_begin_info = {};
		command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		VkCheckResult(vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info));

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

			TextureResource* skybox_texture = data->texture_slotmap.Find(data->skybox_texture_handle);
			//TextureResource* skybox_texture = data->texture_slotmap.Find(data->texture_slotmap.Find(data->skybox_texture_handle)->next);
			//TextureResource* skybox_texture = data->texture_slotmap.Find(data->texture_slotmap.Find(data->texture_slotmap.Find(data->skybox_texture_handle)->next)->next);
			VK_ASSERT(skybox_texture);

			push_consts.env_texture_index = skybox_texture->descriptor.GetIndex();
			push_consts.env_sampler_index = 0;

			data->render_passes.skybox.PushConstants(command_buffer, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push_consts), &push_consts);

			vk_inst.pFunc.cmd_bind_descriptor_buffers_ext(command_buffer, (uint32_t)data->descriptor_buffer_binding_infos.size(), data->descriptor_buffer_binding_infos.data());
			data->render_passes.skybox.SetDescriptorBufferOffsets(command_buffer, 0, 5, &data->descriptor_buffer_indices[0], &data->descriptor_buffer_offsets[0]);
			
			VkDeviceSize vb_offset = 0;
			vkCmdBindVertexBuffers(command_buffer, 0, 1, &data->unit_cube_vertex_buffer.buffer, &vb_offset);
			vkCmdBindIndexBuffer(command_buffer, data->unit_cube_index_buffer.buffer, 0, VK_INDEX_TYPE_UINT16);
			vkCmdDrawIndexed(command_buffer, data->unit_cube_index_buffer.size / sizeof(uint16_t), 1, 0, 0, 0);
		}
		END_PASS(command_buffer, data->render_passes.skybox);

		// ----------------------------------------------------------------------------------------------------------------
		// Lighting pass

		BEGIN_PASS(command_buffer, data->render_passes.lighting, begin_info);
		{
			// Viewport and scissor
			vkCmdSetViewport(command_buffer, 0, 1, &viewport);
			vkCmdSetScissor(command_buffer, 0, 1, &scissor);

			// Push constants
			struct PushConsts
			{
				uint32_t irradiance_cubemap_index;
				uint32_t prefiltered_cubemap_index;
				uint32_t num_prefiltered_mips;
				uint32_t brdf_lut_index;
				uint32_t mat_index;
			} push_consts;

			TextureResource* irradiance_cubemap = data->texture_slotmap.Find(data->texture_slotmap.Find(data->skybox_texture_handle)->next);
			if (!irradiance_cubemap)
			{
				VK_EXCEPT("Renderer::RenderFrame", "HDR environment map is missing an irradiance cubemap");
			}

			TextureResource* prefiltered_cubemap = data->texture_slotmap.Find(data->texture_slotmap.Find(data->texture_slotmap.Find(data->skybox_texture_handle)->next)->next);
			if (!prefiltered_cubemap)
			{
				VK_EXCEPT("Renderer::RenderFrame", "HDR environment map is missing a prefiltered cubemap");
			}

			push_consts.irradiance_cubemap_index = irradiance_cubemap->descriptor.GetIndex();
			push_consts.prefiltered_cubemap_index = prefiltered_cubemap->descriptor.GetIndex();
			push_consts.num_prefiltered_mips = prefiltered_cubemap->view.num_mips - 1;
			push_consts.brdf_lut_index = data->ibl.brdf_lut->descriptor.GetIndex();

			data->render_passes.lighting.PushConstants(command_buffer, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4 * sizeof(uint32_t), &push_consts);

			// Bind descriptor buffers
			vk_inst.pFunc.cmd_bind_descriptor_buffers_ext(command_buffer, (uint32_t)data->descriptor_buffer_binding_infos.size(), data->descriptor_buffer_binding_infos.data());
			data->render_passes.lighting.SetDescriptorBufferOffsets(command_buffer, 0, 5, &data->descriptor_buffer_indices[0], &data->descriptor_buffer_offsets[0]);

			// Instance buffer
			const Vulkan::Buffer& instance_buffer = data->instance_buffers[vk_inst.current_frame];

			for (uint32_t i = 0; i < data->draw_list.next_free_entry; ++i)
			{
				const DrawList::Entry& entry = data->draw_list.entries[i];

				// TODO: Default mesh
				MeshResource* mesh = nullptr;
				if (VK_RESOURCE_HANDLE_VALID(entry.mesh_handle))
				{
					mesh = data->mesh_slotmap.Find(entry.mesh_handle);
				}
				VK_ASSERT(mesh && "Tried to render a mesh with an invalid mesh handle");

				data->render_passes.lighting.PushConstants(command_buffer, VK_SHADER_STAGE_FRAGMENT_BIT, 16, sizeof(uint32_t), &i);

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
		}
		END_PASS(command_buffer, data->render_passes.lighting);

		// ----------------------------------------------------------------------------------------------------------------
		// Post-process pass

		BEGIN_PASS(command_buffer, data->render_passes.post_process, begin_info);
		{
			vk_inst.pFunc.cmd_bind_descriptor_buffers_ext(command_buffer, (uint32_t)data->descriptor_buffer_binding_infos.size(), data->descriptor_buffer_binding_infos.data());
			data->render_passes.post_process.SetDescriptorBufferOffsets(command_buffer, 0, 5, &data->descriptor_buffer_indices[0], &data->descriptor_buffer_offsets[0]);

			uint32_t src_dst_indices[2] = { RESERVED_DESCRIPTOR_STORAGE_IMAGE_HDR, RESERVED_DESCRIPTOR_STORAGE_IMAGE_SDR};
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
				ImGui::Checkbox("Use squared roughness (more linear perceptually)", (bool*)&data->settings.use_squared_roughness);
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("Squares the roughness before doing any lighting calculations, which makes it perceptually more linear");
				}

				ImGui::Checkbox("Use clearcoat", (bool*)&data->settings.use_clearcoat);
				ImGui::Checkbox("Use image-based lighting", (bool*)&data->settings.use_ibl);
				ImGui::Checkbox("Clearcoat specular IBL", (bool*)&data->settings.use_clearcoat_specular_ibl);

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
		VkCommandBuffer command_buffer = data->command_buffers[vk_inst.current_frame];

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

		Vulkan::ImageView swapchain_view;
		Vulkan::Image swapchain_image;
		swapchain_view.image = &swapchain_image;
		swapchain_view.image->image = vk_inst.swapchain.images[vk_inst.swapchain.current_image];
		swapchain_view.image->format = vk_inst.swapchain.format;
		swapchain_view.layout = vk_inst.swapchain.layouts[vk_inst.swapchain.current_image];

		std::vector<VkImageMemoryBarrier2> copy_barriers =
		{
			Vulkan::ImageMemoryBarrier(data->render_targets.sdr->view, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
			Vulkan::ImageMemoryBarrier(swapchain_view, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
		};

		Vulkan::CmdTransitionImageLayouts(command_buffer, copy_barriers);
		vk_inst.swapchain.layouts[vk_inst.swapchain.current_image] = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		vkCmdCopyImage(command_buffer, data->render_targets.sdr->image.image, data->render_targets.sdr->view.layout,
			vk_inst.swapchain.images[vk_inst.swapchain.current_image], vk_inst.swapchain.layouts[vk_inst.swapchain.current_image], 1, &copy_region);

		Vulkan::CmdTransitionImageLayout(command_buffer, swapchain_view, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
		vk_inst.swapchain.layouts[vk_inst.swapchain.current_image] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		// Submit the command buffer for execution
		VkCheckResult(vkEndCommandBuffer(command_buffer));

		VkSubmitInfo submit_info = {};
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

		VkSemaphore wait_semaphores[] = { vk_inst.swapchain.image_available_semaphores[vk_inst.current_frame] };
		VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT };
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
		VkResult result = Vulkan::SwapChainPresent(signal_semaphores);
		if ((result == VK_ERROR_OUT_OF_DATE_KHR) || (result == VK_SUBOPTIMAL_KHR))
		{
			Vulkan::RecreateSwapChain();
			DestroyRenderTargets();
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

		// Reset/Update per-frame data
		data->stats.Reset();
		data->draw_list.Reset();
		vk_inst.current_frame = (vk_inst.current_frame + 1) % VulkanInstance::MAX_FRAMES_IN_FLIGHT;
		data->num_pointlights = 0;
	}

	TextureHandle_t CreateTexture(const CreateTextureArgs& args)
	{
		// Determine the texture byte size
		VkDeviceSize image_size = args.pixels.size();

		// Create staging buffer
		Vulkan::Buffer staging_buffer;
		Vulkan::CreateStagingBuffer(image_size, staging_buffer);

		// Copy data into the mapped memory of the staging buffer
		Vulkan::WriteBuffer(staging_buffer.ptr, (void*)args.pixels.data(), image_size);

		// Create texture image
		ResourceSlotmap<TextureResource>::ReservedResource reserved = data->texture_slotmap.Reserve();

		uint32_t num_mips = 1;
		if (args.generate_mips)
		{
			num_mips = (uint32_t)std::floor(std::log2(std::max(args.width, args.height))) + 1;
		}

		Vulkan::CreateImage(args.width, args.height, TEXTURE_FORMAT_TO_VK_FORMAT[args.format], VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, reserved.resource->image, num_mips);
		Vulkan::CreateImageView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT, &reserved.resource->image, reserved.resource->view, 0, num_mips, 0, 1);

		// Copy staging buffer data into final texture image memory (device local)
		Vulkan::TransitionImageLayoutImmediate(reserved.resource->view, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, num_mips);
		Vulkan::CopyBufferToImage(staging_buffer, reserved.resource->image, args.width, args.height);
		
		// Generate mips
		if (num_mips > 1)
		{
			Vulkan::GenerateMips(args.width, args.height, num_mips, TEXTURE_FORMAT_TO_VK_FORMAT[args.format], reserved.resource->image);
			reserved.resource->view.layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
		}
		else
		{
			// Generate Mips will already transition the image back to READ_ONLY_OPTIMAL, if we do not generate mips, we have to do it manually
			Vulkan::TransitionImageLayoutImmediate(reserved.resource->view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
		}

		// Allocate and update image descriptor
		reserved.resource->descriptor = data->descriptor_buffer_sampled_image.Allocate();
		reserved.resource->descriptor.WriteDescriptor(reserved.resource->view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);

		// Clean up staging buffer
		Vulkan::DestroyBuffer(staging_buffer);

		// Generate irradiance cube map for IBL
		if (args.is_environment_map)
		{
			// TODO: Assumed always using the default sampler at index 0 for now
			// Generate a cubemap from the equirectangular hdr environment map
			Vulkan::Image cubemap_image;
			Vulkan::ImageView cubemap_view;
			GenerateCubeMapFromEquirectangular(reserved.resource->descriptor.GetIndex(), 0, cubemap_image, cubemap_view);

			// Delete the original HDR texture, we don't need it anymore, and swap in the cubemap
			Vulkan::DestroyImageView(reserved.resource->view);
			Vulkan::DestroyImage(reserved.resource->image);

			// Update the cubemap to the reserved resource
			reserved.resource->image = cubemap_image;
			reserved.resource->view = cubemap_view;
			reserved.resource->descriptor.WriteDescriptor(reserved.resource->view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);

			// Generate the irradiance cubemap from the hdr cubemap
			// Reserve another resource for the irradiance cubemap
			ResourceSlotmap<TextureResource>::ReservedResource reserved_irradiance = data->texture_slotmap.Reserve();
			GenerateIrradianceCube(reserved.resource->descriptor.GetIndex(), 0, reserved_irradiance.resource->image, reserved_irradiance.resource->view);

			// Update descriptor
			reserved_irradiance.resource->descriptor = data->descriptor_buffer_sampled_image.Allocate();
			reserved_irradiance.resource->descriptor.WriteDescriptor(reserved_irradiance.resource->view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);

			// Chain the irradiance cubemap to the primary hdr cubemap resource
			reserved.resource->next = reserved_irradiance.handle;

			// Generate the prefiltered cubemap from the hdr cubemap
			// Reserve yet another resource for the prefiltered cubemap
			ResourceSlotmap<TextureResource>::ReservedResource reserved_prefiltered = data->texture_slotmap.Reserve();
			GeneratePrefilteredEnvMap(reserved.resource->descriptor.GetIndex(), 0, reserved_prefiltered.resource->image, reserved_prefiltered.resource->view);

			// Update descriptor
			reserved_prefiltered.resource->descriptor = data->descriptor_buffer_sampled_image.Allocate();
			reserved_prefiltered.resource->descriptor.WriteDescriptor(reserved_prefiltered.resource->view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);

			// Chain the prefiltered cubemap to the irradiance cubemap resource
			reserved_irradiance.resource->next = reserved_prefiltered.handle;
		}

		// Create ImGui descriptor set
		reserved.resource->imgui_descriptor_set = ImGui_ImplVulkan_AddTexture(data->default_sampler, reserved.resource->view.view, reserved.resource->view.layout);

		return reserved.handle;
	}

	void DestroyTexture(TextureHandle_t handle)
	{
		TextureResource* texture = data->texture_slotmap.Find(handle);
		if (!texture)
		{
			VK_EXCEPT("Renderer::DestroyTexture", "Tried to destroy a texture that does not exist");
		}

		// We can currently chain textures together if there are multiple associated, so we need a loop
		// E.g. used with environment maps, 0 = hdr cube, 1 = irradiance cube, 2 = prefiltered cube
		while (texture)
		{
			data->texture_slotmap.Delete(handle);

			if (VK_RESOURCE_HANDLE_VALID(texture->next))
			{
				texture = data->texture_slotmap.Find(texture->next);
			}
			else
			{
				texture = nullptr;
			}
		}
	}

	void ImGuiRenderTexture(TextureHandle_t handle)
	{
		VK_ASSERT(VK_RESOURCE_HANDLE_VALID(handle));
		TextureResource* texture = data->texture_slotmap.Find(handle);
		VK_ASSERT(texture);

		if (texture)
		{
			ImVec2 size = ImVec2(
				std::min((float)vk_inst.swapchain.extent.width / 8.0f, ImGui::GetWindowSize().x),
				std::min((float)vk_inst.swapchain.extent.width / 8.0f, ImGui::GetWindowSize().y)
			);
			ImGui::Image(texture->imgui_descriptor_set, size);
		}
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
		Vulkan::WriteBuffer(staging_buffer.ptr + vertex_buffer_size, (void*)args.indices.data(), index_buffer_size);

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

	void SubmitMesh(MeshHandle_t mesh_handle, const Assets::Material& material, const glm::mat4& transform)
	{
		Vulkan::Buffer& instance_buffer = data->instance_buffers[vk_inst.current_frame];

		DrawList::Entry& entry = data->draw_list.GetNextEntry();
		entry.mesh_handle = mesh_handle;
		entry.transform = transform;

		memcpy((glm::mat4*)instance_buffer.ptr + entry.index, &entry.transform, sizeof(glm::mat4));

		TextureResource* albedo_texture = data->texture_slotmap.Find(material.albedo_texture_handle);
		entry.material_data.albedo_texture_index = albedo_texture ? albedo_texture->descriptor.GetIndex() : data->default_white_texture->descriptor.GetIndex();

		TextureResource* normal_texture = data->texture_slotmap.Find(material.normal_texture_handle);
		entry.material_data.normal_texture_index = normal_texture ? normal_texture->descriptor.GetIndex() : data->default_normal_texture->descriptor.GetIndex();

		TextureResource* metallic_roughness_texture = data->texture_slotmap.Find(material.metallic_roughness_texture_handle);
		entry.material_data.metallic_roughness_texture_index = metallic_roughness_texture ? metallic_roughness_texture->descriptor.GetIndex() : data->default_white_texture->descriptor.GetIndex();

		entry.material_data.albedo_factor = material.albedo_factor;
		entry.material_data.metallic_factor = material.metallic_factor;
		entry.material_data.roughness_factor = material.roughness_factor;

		entry.material_data.has_clearcoat = material.has_clearcoat ? 1 : 0;

		TextureResource* clearcoat_alpha_texture = data->texture_slotmap.Find(material.clearcoat_alpha_texture_handle);
		entry.material_data.clearcoat_alpha_texture_index = clearcoat_alpha_texture ? clearcoat_alpha_texture->descriptor.GetIndex() : data->default_white_texture->descriptor.GetIndex();

		TextureResource* clearcoat_normal_texture = data->texture_slotmap.Find(material.clearcoat_normal_texture_handle);
		entry.material_data.clearcoat_normal_texture_index = clearcoat_normal_texture ? clearcoat_normal_texture->descriptor.GetIndex() : data->default_normal_texture->descriptor.GetIndex();

		TextureResource* clearcoat_roughness_texture = data->texture_slotmap.Find(material.clearcoat_roughness_texture_handle);
		entry.material_data.clearcoat_roughness_texture_index = clearcoat_roughness_texture ? clearcoat_roughness_texture->descriptor.GetIndex() : data->default_white_texture->descriptor.GetIndex();

		entry.material_data.clearcoat_alpha_factor = material.clearcoat_alpha_factor;
		entry.material_data.clearcoat_roughness_factor = material.clearcoat_roughness_factor;

		// Default sampler
		entry.material_data.sampler_index = 0;

		memcpy((MaterialData*)data->ubos.material_buffers[vk_inst.current_frame].ptr + entry.index, &entry.material_data, sizeof(MaterialData));
	}

	void SubmitPointlight(const glm::vec3& pos, const glm::vec3& color, float intensity)
	{
		VK_ASSERT(data->num_pointlights < MAX_LIGHT_SOURCES && "Exceeded the maximum amount of light sources");

		PointlightData* pointlight_data = (PointlightData*)data->ubos.light_buffers[vk_inst.current_frame].ptr;
		pointlight_data[data->num_pointlights].position = pos;
		pointlight_data[data->num_pointlights].intensity = intensity;
		pointlight_data[data->num_pointlights].color = color;

		data->num_pointlights++;
	}

}
