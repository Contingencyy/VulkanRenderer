#include "Precomp.h"
#include "renderer/vulkan/VulkanDescriptor.h"
#include "renderer/vulkan/VulkanInstance.h"
#include "renderer/vulkan/VulkanBuffer.h"
#include "renderer/vulkan/VulkanDeviceMemory.h"
#include "renderer/vulkan/VulkanBackend.h"
#include "renderer/vulkan/VulkanUtils.h"
#include "Shared.glsl.h"

namespace Vulkan
{

	namespace Descriptor
	{

		/*
			============================ PRIVATE FUNCTIONS =================================
		*/

		static constexpr uint32_t DESCRIPTOR_BUFFER_DEFAULT_DESCRIPTOR_COUNT = 16384;

		struct FreeDescriptorBlock
		{
			uint32_t num_descriptors = 0;
			uint32_t descriptor_offset = 0;

			FreeDescriptorBlock* ptr_next = nullptr;
		};

		struct DescriptorBuffer
		{
			VulkanBuffer buffer;
			uint8_t* ptr_mem = nullptr;

			VulkanDescriptorType type = VULKAN_DESCRIPTOR_TYPE_NUM_TYPES;
			VkDescriptorSetLayout vk_descriptor_set_layout = VK_NULL_HANDLE;

			uint32_t num_descriptors = 0;
			uint32_t descriptor_size_in_bytes = 0;

			FreeDescriptorBlock* head = nullptr;
		};

		struct Data
		{
			std::array<DescriptorBuffer, MAX_FRAMES_IN_FLIGHT + VULKAN_DESCRIPTOR_TYPE_NUM_TYPES - 1> descriptor_buffers;
		} static data;

		static VkDescriptorType GetVkDescriptorType(VulkanDescriptorType type)
		{
			switch (type)
			{
			case VULKAN_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
				return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			case VULKAN_DESCRIPTOR_TYPE_STORAGE_BUFFER:
				return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			case VULKAN_DESCRIPTOR_TYPE_STORAGE_IMAGE:
				return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			case VULKAN_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
				return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
			case VULKAN_DESCRIPTOR_TYPE_SAMPLER:
				return VK_DESCRIPTOR_TYPE_SAMPLER;
			case VULKAN_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE:
				return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
			default:
				VK_EXCEPT("Descriptor::GetVkDescriptorType", "Tried to get the VkDescriptorType for an unknown descriptor type");
			}
		}

		static uint32_t GetDescriptorTypeByteSize(VulkanDescriptorType type)
		{
			switch (type)
			{
			case VULKAN_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
				return vk_inst.descriptor_sizes.uniform_buffer;
			case VULKAN_DESCRIPTOR_TYPE_STORAGE_BUFFER:
				return vk_inst.descriptor_sizes.storage_buffer;
			case VULKAN_DESCRIPTOR_TYPE_STORAGE_IMAGE:
				return vk_inst.descriptor_sizes.storage_image;
			case VULKAN_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
				return vk_inst.descriptor_sizes.sampled_image;
			case VULKAN_DESCRIPTOR_TYPE_SAMPLER:
				return vk_inst.descriptor_sizes.sampler;
			case VULKAN_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE:
				return vk_inst.descriptor_sizes.acceleration_structure;
			default:
				VK_EXCEPT("Descriptor::GetDescriptorTypeSize", "Tried to get the descriptor size for an unknown descriptor type");
			}
		}

		static inline DescriptorBuffer& GetDescriptorBuffer(VulkanDescriptorType type, uint32_t frame_index)
		{
			switch (type)
			{
			case VULKAN_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
				return data.descriptor_buffers[type + frame_index];
			case VULKAN_DESCRIPTOR_TYPE_STORAGE_BUFFER:
			case VULKAN_DESCRIPTOR_TYPE_STORAGE_IMAGE:
			case VULKAN_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
			case VULKAN_DESCRIPTOR_TYPE_SAMPLER:
			case VULKAN_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE:
				return data.descriptor_buffers[type + MAX_FRAMES_IN_FLIGHT - 1];
			default:
				VK_EXCEPT("Descriptor::GetDescriptorBuffer", "Tried to get the descriptor buffer for an unknown descriptor type");
			}
		}

		static VkDescriptorBufferBindingInfoEXT GetDescriptorBufferBindingInfo(VulkanDescriptorType type, uint32_t frame_index)
		{
			const DescriptorBuffer& descriptor_buffer = GetDescriptorBuffer(type, frame_index);

			VkBufferDeviceAddressInfo buffer_device_address_info = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
			buffer_device_address_info.buffer = descriptor_buffer.buffer.vk_buffer;
			VkDeviceAddress buffer_device_address = vkGetBufferDeviceAddress(vk_inst.device, &buffer_device_address_info);

			VkDescriptorBufferBindingInfoEXT descriptor_buffer_binding_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT };
			descriptor_buffer_binding_info.usage = type != VULKAN_DESCRIPTOR_TYPE_SAMPLER ?
				VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT : VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT;
			descriptor_buffer_binding_info.address = buffer_device_address;
			
			return descriptor_buffer_binding_info;
		}

		static void CreateDescriptorBuffers()
		{
			uint32_t current_type = VULKAN_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

			// Create the descriptor type buffers and their free lists from which we will allocate
			for (uint32_t buffer = 0; buffer < data.descriptor_buffers.size(); ++buffer)
			{
				// Create the descriptor set layout
				std::vector<VkDescriptorSetLayoutBinding> bindings;
				uint32_t num_descriptors_in_buffer = 0;

				if (buffer < MAX_FRAMES_IN_FLIGHT)
				{
					num_descriptors_in_buffer = RESERVED_DESCRIPTOR_UBO_COUNT;
					bindings.resize(num_descriptors_in_buffer);

					for (uint32_t ubo = 0; ubo < bindings.size(); ++ubo)
					{
						bindings[ubo].binding = ubo;
						bindings[ubo].descriptorCount = 1;
						bindings[ubo].descriptorType = GetVkDescriptorType((VulkanDescriptorType)current_type);
						bindings[ubo].pImmutableSamplers = nullptr;
						bindings[ubo].stageFlags = VK_SHADER_STAGE_ALL;
					}
				}
				else
				{
					num_descriptors_in_buffer = DESCRIPTOR_BUFFER_DEFAULT_DESCRIPTOR_COUNT;
					current_type++;

					bindings.resize(1);
					bindings[0].binding = 0;
					bindings[0].descriptorCount = num_descriptors_in_buffer;
					bindings[0].descriptorType = GetVkDescriptorType((VulkanDescriptorType)current_type);
					bindings[0].pImmutableSamplers = nullptr;
					bindings[0].stageFlags = VK_SHADER_STAGE_ALL;
				}

				VkDescriptorSetLayoutBindingFlagsCreateInfo binding_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO };
				binding_info.bindingCount = 0;
				binding_info.pBindingFlags = nullptr;

				VkDescriptorSetLayoutCreateInfo layout_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
				layout_info.bindingCount = static_cast<uint32_t>(bindings.size());
				layout_info.pBindings = bindings.data();
				layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
				layout_info.pNext = &binding_info;
				
				VkDescriptorSetLayout vk_descriptor_set_layout = VK_NULL_HANDLE;
				Vulkan::VkCheckResult(vkCreateDescriptorSetLayout(vk_inst.device, &layout_info, nullptr, &vk_descriptor_set_layout));

				uint64_t descriptor_layout_byte_size;
				vk_inst.pFunc.get_descriptor_set_layout_size_ext(vk_inst.device, vk_descriptor_set_layout, &descriptor_layout_byte_size);

				// Create the underlying descriptor buffer resource
				Flags buffer_usage_flags = current_type != VULKAN_DESCRIPTOR_TYPE_SAMPLER ? BUFFER_USAGE_RESOURCE_DESCRIPTORS : BUFFER_USAGE_SAMPLER_DESCRIPTORS;
				BufferCreateInfo buffer_info = {
					.usage_flags = buffer_usage_flags,
					.memory_flags = GPU_MEMORY_HOST_VISIBLE | GPU_MEMORY_HOST_COHERENT,
					.size_in_bytes = descriptor_layout_byte_size,
					.name = "Descriptor Buffer"
				};

				// Create the descriptor buffer
				DescriptorBuffer& descriptor_buffer = data.descriptor_buffers[buffer];
				descriptor_buffer.buffer = Buffer::Create(buffer_info);
				descriptor_buffer.ptr_mem = reinterpret_cast<uint8_t*>(DeviceMemory::Map(descriptor_buffer.buffer.memory, descriptor_layout_byte_size, 0));
				descriptor_buffer.type = (VulkanDescriptorType)current_type;
				descriptor_buffer.vk_descriptor_set_layout = vk_descriptor_set_layout;

				descriptor_buffer.num_descriptors = num_descriptors_in_buffer;
				descriptor_buffer.descriptor_size_in_bytes = GetDescriptorTypeByteSize(descriptor_buffer.type);

				// Allocate the head of the free-list for this descriptor buffer
				FreeDescriptorBlock* head = new FreeDescriptorBlock();
				head->num_descriptors = descriptor_buffer.num_descriptors;
				head->descriptor_offset = 0;
				head->ptr_next = nullptr;
				descriptor_buffer.head = head;
			}
		}

		static uint32_t AllocateDescriptors(VulkanDescriptorType type, uint32_t num_descriptors, uint32_t frame_index)
		{
			DescriptorBuffer& descriptor_buffer = GetDescriptorBuffer(type, frame_index);
			FreeDescriptorBlock* previous_block = nullptr;
			FreeDescriptorBlock* free_block = descriptor_buffer.head;

			while (free_block)
			{
				// Free block fits the allocation request perfectly
				if (free_block->num_descriptors == num_descriptors)
				{
					uint32_t alloc_offset = 0;

					// Point the previous block to the next block of the current block
					if (previous_block)
					{
						previous_block->ptr_next = free_block->ptr_next;
						alloc_offset = free_block->descriptor_offset;
						delete free_block;
					}
					// Point the descriptor buffer head to the next block of the current block
					else
					{
						alloc_offset = descriptor_buffer.head->descriptor_offset;
						descriptor_buffer.head = free_block->ptr_next;
						delete free_block;
					}

					return alloc_offset;
				}
				// Free block can fit the allocation, resize it
				else if (free_block->num_descriptors > num_descriptors)
				{
					uint32_t alloc_offset = free_block->descriptor_offset;
					free_block->num_descriptors -= num_descriptors;
					free_block->descriptor_offset += num_descriptors;

					return alloc_offset;
				}

				previous_block = free_block;
				free_block = free_block->ptr_next;
			}

			VK_EXCEPT("Descriptor::AllocateDescriptors", "Could not find a free block large enough to fit the requested amount of descriptors");
			return UINT64_MAX;
		}

		static void FreeDescriptors(VulkanDescriptorType type, uint32_t offset_in_descriptors, uint32_t num_descriptors, uint32_t frame_index)
		{
			DescriptorBuffer& descriptor_buffer = GetDescriptorBuffer(type, frame_index);
			FreeDescriptorBlock* previous_block = nullptr;
			FreeDescriptorBlock* free_block = descriptor_buffer.head;

			if (!free_block)
			{
				// If there is no free block at all, it means that the whole freelist is allocated/occupied
				// So we need to create a new free block at the head
				FreeDescriptorBlock* new_block = new FreeDescriptorBlock();
				new_block->num_descriptors = num_descriptors;
				new_block->descriptor_offset = offset_in_descriptors;
				new_block->ptr_next = nullptr;

				descriptor_buffer.head = new_block;
				return;
			}

			while (free_block)
			{
				// If the current free block is on the left of the descriptors to be freed,
				// the descriptors to be freed can be appended to the right of it
				if (free_block->descriptor_offset + free_block->num_descriptors == offset_in_descriptors)
				{
					free_block->num_descriptors += num_descriptors;

					// If the free block after our merged one is now directly adjacent, merge it in as well
					if (free_block->ptr_next &&
						free_block->ptr_next->descriptor_offset == free_block->descriptor_offset + free_block->num_descriptors)
					{
						free_block->num_descriptors += free_block->ptr_next->num_descriptors;
						FreeDescriptorBlock* next_block = free_block->ptr_next;
						free_block->ptr_next = next_block->ptr_next;
					}

					return;
				}
				// Need a new free descriptor block to be inserted
				else if (free_block->descriptor_offset > offset_in_descriptors)
				{
					FreeDescriptorBlock* new_block = new FreeDescriptorBlock();
					new_block->num_descriptors = num_descriptors;
					new_block->descriptor_offset = offset_in_descriptors;

					// If there is a previous block, append the new block to the chain
					if (previous_block)
					{
						previous_block->ptr_next = new_block;
						new_block->ptr_next = free_block;
					}
					// If there is no previous block, append the new block to the head
					else
					{
						new_block->ptr_next = free_block;
						descriptor_buffer.head = new_block;
					}

					// Check if the new block and the block after can be merged
					if (new_block->ptr_next &&
						new_block->descriptor_offset + new_block->num_descriptors == new_block->ptr_next->descriptor_offset)
					{
						new_block->num_descriptors += new_block->ptr_next->num_descriptors;
						FreeDescriptorBlock* next_block = new_block->ptr_next;
						new_block->ptr_next = next_block->ptr_next;
						delete next_block;
					}

					// Check if the new block and the block before can be merged
					if (previous_block &&
						previous_block->descriptor_offset + previous_block->num_descriptors == new_block->descriptor_offset)
					{
						previous_block->num_descriptors += new_block->num_descriptors;
						previous_block->ptr_next = new_block->ptr_next;
						delete new_block;
					}

					return;
				}
				else if (free_block->descriptor_offset == offset_in_descriptors)
				{
					VK_EXCEPT("Descriptor::FreeDescriptors", "Tried to free the same descriptor allocation multiple times");
				}

				previous_block = free_block;
				free_block = free_block->ptr_next;
			}

			VK_EXCEPT("Descriptor::FreeDescriptors", "Unable to free descriptor block, which should always be possible");
		}

		/*
			============================ PUBLIC INTERFACE FUNCTIONS =================================
		*/

		void Init()
		{
			CreateDescriptorBuffers();
		}

		void Exit()
		{
			for (uint32_t i = 0; i < data.descriptor_buffers.size(); ++i)
			{
				DescriptorBuffer& descriptor_buffer = data.descriptor_buffers[i];
				FreeDescriptorBlock* current_block = descriptor_buffer.head;

				while (current_block)
				{
					// Free all free blocks remaining in the descriptor buffers
					FreeDescriptorBlock* next_block = current_block->ptr_next;
					delete current_block;
					current_block = next_block;
				}

				vkDestroyDescriptorSetLayout(vk_inst.device, descriptor_buffer.vk_descriptor_set_layout, nullptr);
				Vulkan::DeviceMemory::Unmap(descriptor_buffer.buffer.memory);
				Vulkan::Buffer::Destroy(descriptor_buffer.buffer);
			}
		}

		VulkanDescriptorAllocation Allocate(VulkanDescriptorType type, uint32_t num_descriptors, uint32_t frame_index)
		{
			const DescriptorBuffer& descriptor_buffer = GetDescriptorBuffer(type, frame_index);

			VulkanDescriptorAllocation alloc = {};
			alloc.type = type;
			alloc.num_descriptors = num_descriptors;
			alloc.descriptor_size_in_bytes = GetDescriptorTypeByteSize(type);
			alloc.descriptor_offset = AllocateDescriptors(type, num_descriptors, frame_index);
			alloc.ptr = descriptor_buffer.ptr_mem + alloc.descriptor_offset * alloc.descriptor_size_in_bytes;

			return alloc;
		}

		void Free(VulkanDescriptorAllocation& alloc, uint32_t frame_index)
		{
			if (!IsValid(alloc))
				return;

			FreeDescriptors(alloc.type, alloc.descriptor_offset, alloc.num_descriptors, frame_index);
			alloc = {};
		}

		void Write(const VulkanDescriptorAllocation& descriptors, const VulkanBuffer& buffer, uint32_t descriptor_offset)
		{
			uint8_t* ptr_descriptor = descriptors.ptr + descriptor_offset * descriptors.descriptor_size_in_bytes;

			VkDescriptorGetInfoEXT descriptor_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT };
			descriptor_info.type = GetVkDescriptorType(descriptors.type);

			VkBufferDeviceAddressInfo buffer_device_address_info = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
			buffer_device_address_info.buffer = buffer.vk_buffer;
			VkDeviceAddress buffer_device_address = vkGetBufferDeviceAddress(vk_inst.device, &buffer_device_address_info);
			buffer_device_address += buffer.offset_in_bytes;
			
			VkDescriptorAddressInfoEXT descriptor_address_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT };
			descriptor_address_info.address = buffer_device_address;
			descriptor_address_info.format = VK_FORMAT_UNDEFINED;
			descriptor_address_info.range = buffer.size_in_bytes;

			switch (descriptors.type)
			{
			case VULKAN_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
			{
				descriptor_info.data.pUniformBuffer = &descriptor_address_info;
			} break;
			case VULKAN_DESCRIPTOR_TYPE_STORAGE_BUFFER:
			{
				descriptor_info.data.pStorageBuffer = &descriptor_address_info;
			} break;
			case VULKAN_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE:
			{
				descriptor_info.data.accelerationStructure = Util::GetAccelerationStructureDeviceAddress(buffer.vk_acceleration_structure);
			} break;
			default:
				VK_EXCEPT("Descriptor::Write", "Tried to write a buffer descriptor for an invalid descriptor type");
			}

			size_t descriptor_size = GetDescriptorTypeByteSize(descriptors.type);
			vk_inst.pFunc.get_descriptor_ext(vk_inst.device, &descriptor_info, descriptor_size, ptr_descriptor);
		}

		void Write(const VulkanDescriptorAllocation& descriptors, const VulkanImageView& view, VkImageLayout layout, uint32_t descriptor_offset)
		{
			uint8_t* ptr_descriptor = descriptors.ptr + descriptor_offset * descriptors.descriptor_size_in_bytes;

			VkDescriptorGetInfoEXT descriptor_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT };
			descriptor_info.type = GetVkDescriptorType(descriptors.type);
			size_t descriptor_size = GetDescriptorTypeByteSize(descriptors.type);

			VkDescriptorImageInfo descriptor_image_info = {};
			descriptor_image_info.imageView = view.vk_image_view;
			descriptor_image_info.imageLayout = layout;
			descriptor_image_info.sampler = VK_NULL_HANDLE;

			switch (descriptors.type)
			{
			case VULKAN_DESCRIPTOR_TYPE_STORAGE_IMAGE:
			{
				descriptor_info.data.pStorageImage = &descriptor_image_info;
			} break;
			case VULKAN_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
			{
				descriptor_info.data.pSampledImage = &descriptor_image_info;
			} break;
			default:
				VK_EXCEPT("Descriptor::Write", "Tried to write an image descriptor for an invalid descriptor type");
			}

			vk_inst.pFunc.get_descriptor_ext(vk_inst.device, &descriptor_info, descriptor_size, ptr_descriptor);
		}

		void Write(const VulkanDescriptorAllocation& descriptors, const VulkanSampler& sampler, uint32_t descriptor_offset)
		{
			uint8_t* ptr_descriptor = descriptors.ptr + descriptor_offset * descriptors.descriptor_size_in_bytes;

			VkDescriptorGetInfoEXT descriptor_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT };
			descriptor_info.type = GetVkDescriptorType(descriptors.type);

			switch (descriptors.type)
			{
			case VULKAN_DESCRIPTOR_TYPE_SAMPLER:
			{
				descriptor_info.data.pSampler = &sampler.vk_sampler;
			} break;
			default:
				VK_EXCEPT("Descriptor::Write", "Tried to write a sampler descriptor for an invalid descriptor type");
			}

			size_t descriptor_size = GetDescriptorTypeByteSize(descriptors.type);
			vk_inst.pFunc.get_descriptor_ext(vk_inst.device, &descriptor_info, descriptor_size, ptr_descriptor);
		}

		bool IsValid(const VulkanDescriptorAllocation& alloc)
		{
			return (
				alloc.type != VULKAN_DESCRIPTOR_TYPE_NUM_TYPES &&
				alloc.num_descriptors > 0 &&
				alloc.descriptor_size_in_bytes > 0 &&
				alloc.ptr
			);
		}

		std::vector<VkDescriptorSetLayout> GetDescriptorSetLayouts()
		{
			std::vector<VkDescriptorSetLayout> descriptor_set_layouts;
			// Add the descriptor set layout for the UBO descriptors
			descriptor_set_layouts.push_back(data.descriptor_buffers[0].vk_descriptor_set_layout);

			// Add all the other descriptor set layouts for the other descriptor types
			for (uint32_t i = MAX_FRAMES_IN_FLIGHT; i < data.descriptor_buffers.size(); ++i)
			{
				descriptor_set_layouts.push_back(data.descriptor_buffers[i].vk_descriptor_set_layout);
			}

			return descriptor_set_layouts;
		}

		void BindDescriptors(const VulkanCommandBuffer& command_buffer, const VulkanPipeline& pipeline)
		{
			// Bind the descriptor buffers
			std::vector<VkDescriptorBufferBindingInfoEXT> descriptor_buffer_binding_infos(VULKAN_DESCRIPTOR_TYPE_NUM_TYPES);
			for (uint32_t i = 0; i < VULKAN_DESCRIPTOR_TYPE_NUM_TYPES; ++i)
				descriptor_buffer_binding_infos[i] = GetDescriptorBufferBindingInfo((VulkanDescriptorType)i, Vulkan::GetCurrentFrameIndex() % MAX_FRAMES_IN_FLIGHT);

			vk_inst.pFunc.cmd_bind_descriptor_buffers_ext(command_buffer.vk_command_buffer,
				static_cast<uint32_t>(descriptor_buffer_binding_infos.size()), descriptor_buffer_binding_infos.data());

			// Set the descriptor buffer indices and offsets
			std::vector<uint32_t> buffer_indices(VULKAN_DESCRIPTOR_TYPE_NUM_TYPES);
			std::vector<uint64_t> buffer_offsets(VULKAN_DESCRIPTOR_TYPE_NUM_TYPES);

			for (uint32_t i = 0; i < VULKAN_DESCRIPTOR_TYPE_NUM_TYPES; ++i)
			{
				buffer_indices[i] = i;
				buffer_offsets[i] = 0;
			}

			vk_inst.pFunc.cmd_set_descriptor_buffer_offsets_ext(command_buffer.vk_command_buffer,
				Util::ToVkPipelineBindPoint(pipeline.type), pipeline.vk_pipeline_layout, 0, VULKAN_DESCRIPTOR_TYPE_NUM_TYPES, buffer_indices.data(), buffer_offsets.data());
		}

	}

}
