#include "Precomp.h"
#include "renderer/vulkan/VulkanDescriptor.h"
#include "renderer/vulkan/VulkanInstance.h"
#include "renderer/vulkan/VulkanBuffer.h"
#include "renderer/vulkan/VulkanDeviceMemory.h"
#include "renderer/vulkan/VulkanUtils.h"

namespace Vulkan
{

	namespace Descriptor
	{

		/*
			============================ PRIVATE FUNCTIONS =================================
		*/

		static constexpr uint32_t DESCRIPTOR_RANGE_DEFAULT_DESCRIPTOR_COUNT = 1024;

		struct FreeDescriptorBlock
		{
			uint32_t num_descriptors = 0;
			uint32_t descriptor_offset = 0;

			FreeDescriptorBlock* ptr_next = nullptr;
		};

		struct DescriptorRange
		{
			VulkanDescriptorType type = VULKAN_DESCRIPTOR_TYPE_NUM_TYPES;
			VkDescriptorSetLayout vk_descriptor_set_layout = VK_NULL_HANDLE;

			uint32_t num_descriptors = 0;
			uint32_t descriptor_size_in_bytes = 0;

			uint64_t size_in_bytes = 0;
			uint64_t offset_in_bytes = 0;

			FreeDescriptorBlock* head = nullptr;
		};

		struct Data
		{
			VulkanBuffer descriptor_buffer;
			uint8_t* mapped_ptr = nullptr;

			DescriptorRange descriptor_ranges[VULKAN_DESCRIPTOR_TYPE_NUM_TYPES];
		} static data;

		static VkDescriptorType GetVkDescriptorType(VulkanDescriptorType type)
		{
			switch (type)
			{
			case VULKAN_DESCRIPTOR_TYPE_UNIFORM:
				return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			case VULKAN_DESCRIPTOR_TYPE_STORAGE_BUFFER:
				return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			case VULKAN_DESCRIPTOR_TYPE_STORAGE_IMAGE:
				return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			case VULKAN_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
				return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
			case VULKAN_DESCRIPTOR_TYPE_SAMPLER:
				return VK_DESCRIPTOR_TYPE_SAMPLER;
			default:
				VK_EXCEPT("Descriptor::GetVkDescriptorType", "Tried to get the VkDescriptorType for an unknown descriptor type");
			}
		}

		static uint64_t CreateDescriptorRanges()
		{
			uint64_t current_byte_offset = 0;

			// Create the descriptor type ranges and their free lists from which we will allocate
			for (uint32_t i = 0; i < VULKAN_DESCRIPTOR_TYPE_NUM_TYPES; ++i)
			{
				current_byte_offset = VK_ALIGN_POW2(current_byte_offset, vk_inst.device_props.descriptor_buffer_offset_alignment);

				// Create the descriptor set layout
				VkDescriptorSetLayoutBinding binding = {};
				binding.binding = 0;
				binding.descriptorCount = DESCRIPTOR_RANGE_DEFAULT_DESCRIPTOR_COUNT;
				binding.descriptorType = GetVkDescriptorType((VulkanDescriptorType)i);
				binding.pImmutableSamplers = nullptr;
				binding.stageFlags = VK_SHADER_STAGE_ALL;

				VkDescriptorSetLayoutBindingFlagsCreateInfo binding_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO };
				binding_info.bindingCount = 0;
				binding_info.pBindingFlags = nullptr;

				VkDescriptorSetLayoutCreateInfo layout_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
				layout_info.bindingCount = 1;
				layout_info.pBindings = &binding;
				layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
				layout_info.pNext = &binding_info;
				
				VkDescriptorSetLayout vk_descriptor_set_layout = VK_NULL_HANDLE;
				Vulkan::VkCheckResult(vkCreateDescriptorSetLayout(vk_inst.device, &layout_info, nullptr, &vk_descriptor_set_layout));

				// Create the descriptor range
				DescriptorRange& descriptor_range = data.descriptor_ranges[i];
				descriptor_range.type = (VulkanDescriptorType)i;
				descriptor_range.vk_descriptor_set_layout = vk_descriptor_set_layout;
				descriptor_range.num_descriptors = binding.descriptorCount;
				descriptor_range.descriptor_size_in_bytes = GetDescriptorTypeByteSize(descriptor_range.type);
				descriptor_range.size_in_bytes = static_cast<uint64_t>(descriptor_range.num_descriptors) *
					static_cast<uint64_t>(descriptor_range.descriptor_size_in_bytes);
				descriptor_range.offset_in_bytes = current_byte_offset;

				uint64_t descriptor_layout_byte_size;
				vk_inst.pFunc.get_descriptor_set_layout_size_ext(vk_inst.device, vk_descriptor_set_layout, &descriptor_layout_byte_size);

				// Allocate the head of the free-list for this descriptor range
				FreeDescriptorBlock* head = new FreeDescriptorBlock();
				head->num_descriptors = descriptor_range.num_descriptors;
				head->descriptor_offset = 0;
				head->ptr_next = nullptr;
				descriptor_range.head = head;

				current_byte_offset += descriptor_range.size_in_bytes;
			}
		}

		static uint64_t CalculateDescriptorBufferSize()
		{
			uint64_t current_byte_size = 0;

			for (uint32_t i = 0; i < VULKAN_DESCRIPTOR_TYPE_NUM_TYPES; ++i)
			{
				current_byte_size = VK_ALIGN_POW2(current_byte_size, vk_inst.device_props.descriptor_buffer_offset_alignment);

				DescriptorRange& descriptor_range = data.descriptor_ranges[i];
				current_byte_size += descriptor_range.size_in_bytes;
			}
		}

		static uint32_t GetDescriptorTypeByteSize(VulkanDescriptorType type)
		{
			switch (type)
			{
			case VULKAN_DESCRIPTOR_TYPE_UNIFORM:
				return vk_inst.descriptor_sizes.uniform_buffer;
			case VULKAN_DESCRIPTOR_TYPE_STORAGE_BUFFER:
				return vk_inst.descriptor_sizes.storage_buffer;
			case VULKAN_DESCRIPTOR_TYPE_STORAGE_IMAGE:
				return vk_inst.descriptor_sizes.storage_image;
			case VULKAN_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
				return vk_inst.descriptor_sizes.sampled_image;
			case VULKAN_DESCRIPTOR_TYPE_SAMPLER:
				return vk_inst.descriptor_sizes.sampler;
			default:
				VK_EXCEPT("Descriptor::GetDescriptorTypeSize", "Tried to get the descriptor size for an unknown descriptor type");
			}
		}

		static uint64_t AllocateDescriptors(VulkanDescriptorType type, uint32_t num_descriptors)
		{
			DescriptorRange& descriptor_range = data.descriptor_ranges[type];
			FreeDescriptorBlock* previous_block = nullptr;
			FreeDescriptorBlock* free_block = descriptor_range.head;

			while (free_block)
			{
				// Free block fits the allocation request perfectly
				if (free_block->num_descriptors == num_descriptors)
				{
					// Point the previous block to the next block of the current block
					if (previous_block)
					{
						previous_block->ptr_next = free_block->ptr_next;
						delete free_block;
						return free_block->descriptor_offset;
					}
					// Point the descriptor range head to the next block of the current block
					else
					{
						delete descriptor_range.head;
						descriptor_range.head = free_block->ptr_next;
						return descriptor_range.head->descriptor_offset;
					}
				}
				// Free block can fit the allocation, resize it
				else if (free_block->num_descriptors > num_descriptors)
				{
					free_block->num_descriptors -= num_descriptors;
					free_block->descriptor_offset += num_descriptors;
					return free_block->descriptor_offset;
				}

				previous_block = free_block;
				free_block = free_block->ptr_next;
			}

			VK_EXCEPT("Descriptor::AllocateDescriptors", "Could not find a free block large enough to fit the requested amount of descriptors");
			return UINT64_MAX;
		}

		static void FreeDescriptors(VulkanDescriptorType type, uint32_t offset_in_descriptors, uint32_t num_descriptors)
		{
			DescriptorRange& descriptor_range = data.descriptor_ranges[type];
			FreeDescriptorBlock* previous_block = nullptr;
			FreeDescriptorBlock* free_block = descriptor_range.head;

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
						descriptor_range.head = new_block;
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
				}
				else if (free_block->descriptor_offset == offset_in_descriptors)
				{
					VK_EXCEPT("Descriptor::FreeDescriptors", "Tried to free the same descriptor allocation multiple times");
				}
			}

			VK_EXCEPT("Descriptor::FreeDescriptors", "Unable to free descriptor block, which should always be possible");
		}

		/*
			============================ PUBLIC INTERFACE FUNCTIONS =================================
		*/

		void Init()
		{
			CreateDescriptorRanges();

			// Create the underlying descriptor buffer resource
			BufferCreateInfo buffer_info = {
				.usage_flags = BUFFER_USAGE_DESCRIPTOR,
				.memory_flags = GPU_MEMORY_HOST_VISIBLE | GPU_MEMORY_HOST_COHERENT,
				.size_in_bytes = CalculateDescriptorBufferSize(),
				.name = "Descriptor Buffer"
			};

			data.descriptor_buffer = Vulkan::Buffer::Create(buffer_info);
			data.mapped_ptr = reinterpret_cast<uint8_t*>(Vulkan::DeviceMemory::Map(
				data.descriptor_buffer.memory, data.descriptor_buffer.size_in_bytes, 0));
		}

		void Exit()
		{
			for (uint32_t i = 0; i < VULKAN_DESCRIPTOR_TYPE_NUM_TYPES; ++i)
			{
				DescriptorRange& descriptor_range = data.descriptor_ranges[i];
				FreeDescriptorBlock* current_block = descriptor_range.head;

				while (current_block)
				{
					vkDestroyDescriptorSetLayout(vk_inst.device, descriptor_range.vk_descriptor_set_layout, nullptr);

					// Free all free blocks remaining in the descriptor ranges
					FreeDescriptorBlock* next_block = current_block->ptr_next;
					delete current_block;
					current_block = next_block;
				}
			}

			Vulkan::DeviceMemory::Unmap(data.descriptor_buffer.memory);
			Vulkan::DeviceMemory::Free(data.descriptor_buffer.memory);
			Vulkan::Buffer::Destroy(data.descriptor_buffer);
		}

		VulkanDescriptorAllocation Allocate(VulkanDescriptorType type, uint32_t num_descriptors)
		{
			VulkanDescriptorAllocation alloc = {};
			alloc.type = type;
			alloc.num_descriptors = num_descriptors;
			alloc.descriptor_size_in_bytes = GetDescriptorTypeByteSize(type);
			alloc.descriptor_offset = AllocateDescriptors(type, num_descriptors);
			alloc.ptr = data.mapped_ptr + data.descriptor_ranges[type].offset_in_bytes + alloc.descriptor_offset * alloc.descriptor_size_in_bytes;

			return alloc;
		}

		void Free(const VulkanDescriptorAllocation& alloc)
		{
			FreeDescriptors(alloc.type, alloc.descriptor_offset, alloc.num_descriptors);
		}

		void Write(const VulkanDescriptorAllocation& descriptors, const VulkanBuffer& buffer, uint32_t descriptor_offset)
		{
			uint8_t* ptr_descriptor = descriptors.ptr + descriptor_offset * descriptors.descriptor_size_in_bytes;

			VkDescriptorGetInfoEXT descriptor_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT };
			descriptor_info.type = GetVkDescriptorType(descriptors.type);
			size_t descriptor_size = GetDescriptorTypeByteSize(descriptors.type);

			switch (descriptors.type)
			{
			case VULKAN_DESCRIPTOR_TYPE_UNIFORM:
			{
				VkBufferDeviceAddressInfo buffer_device_address_info = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
				buffer_device_address_info.buffer = buffer.vk_buffer;

				VkDescriptorAddressInfoEXT descriptor_address_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT };
				descriptor_address_info.address = vkGetBufferDeviceAddress(vk_inst.device, &buffer_device_address_info);
				descriptor_address_info.format = VK_FORMAT_UNDEFINED;
				descriptor_address_info.range = buffer.size_in_bytes;

				descriptor_info.data.pUniformBuffer = &descriptor_address_info;
			} break;
			case VULKAN_DESCRIPTOR_TYPE_STORAGE_BUFFER:
			{
				VkBufferDeviceAddressInfo buffer_device_address_info = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
				buffer_device_address_info.buffer = buffer.vk_buffer;

				VkDescriptorAddressInfoEXT descriptor_address_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT };
				descriptor_address_info.address = vkGetBufferDeviceAddress(vk_inst.device, &buffer_device_address_info);
				descriptor_address_info.format = VK_FORMAT_UNDEFINED;
				descriptor_address_info.range = buffer.size_in_bytes;

				descriptor_info.data.pStorageBuffer = &descriptor_address_info;
			} break;
			default:
				VK_EXCEPT("Descriptor::Write", "Tried to write a buffer descriptor for an invalid descriptor type");
			}

			vkGetDescriptorEXT(vk_inst.device, &descriptor_info, descriptor_size, ptr_descriptor);
		}

		void Write(const VulkanDescriptorAllocation& descriptors, const VulkanImageView& view, VkImageLayout layout, uint32_t descriptor_offset)
		{
			uint8_t* ptr_descriptor = descriptors.ptr + descriptor_offset * descriptors.descriptor_size_in_bytes;

			VkDescriptorGetInfoEXT descriptor_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT };
			descriptor_info.type = GetVkDescriptorType(descriptors.type);
			size_t descriptor_size = GetDescriptorTypeByteSize(descriptors.type);
			
			switch (descriptors.type)
			{
			case VULKAN_DESCRIPTOR_TYPE_STORAGE_IMAGE:
			{
				VkDescriptorImageInfo descriptor_image_info = {};
				descriptor_image_info.imageView = view.vk_image_view;
				descriptor_image_info.imageLayout = layout;
				descriptor_image_info.sampler = VK_NULL_HANDLE;

				descriptor_info.data.pStorageImage = &descriptor_image_info;
			} break;
			case VULKAN_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
			{
				VkDescriptorImageInfo descriptor_image_info = {};
				descriptor_image_info.imageView = view.vk_image_view;
				descriptor_image_info.imageLayout = layout;
				descriptor_image_info.sampler = VK_NULL_HANDLE;

				descriptor_info.data.pSampledImage = &descriptor_image_info;
			} break;
			default:
				VK_EXCEPT("Descriptor::Write", "Tried to write an image descriptor for an invalid descriptor type");
			}

			vkGetDescriptorEXT(vk_inst.device, &descriptor_info, descriptor_size, ptr_descriptor);
		}

		void Write(const VulkanDescriptorAllocation& descriptors, const VulkanSampler& sampler, uint32_t descriptor_offset)
		{
			uint8_t* ptr_descriptor = descriptors.ptr + descriptor_offset * descriptors.descriptor_size_in_bytes;

			VkDescriptorGetInfoEXT descriptor_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT };
			descriptor_info.type = GetVkDescriptorType(descriptors.type);
			size_t descriptor_size = GetDescriptorTypeByteSize(descriptors.type);

			switch (descriptors.type)
			{
			case VULKAN_DESCRIPTOR_TYPE_SAMPLER:
			{
				descriptor_info.data.pSampler = &sampler.vk_sampler;
			} break;
			default:
				VK_EXCEPT("Descriptor::Write", "Tried to write a sampler descriptor for an invalid descriptor type");
			}

			vkGetDescriptorEXT(vk_inst.device, &descriptor_info, descriptor_size, ptr_descriptor);
		}

		bool IsValid(const VulkanDescriptorAllocation& alloc)
		{
			return (
				alloc.type != VULKAN_DESCRIPTOR_TYPE_NUM_TYPES &&
				alloc.num_descriptors > 0 &&
				alloc.descriptor_size_in_bytes > 0
			);
		}

	}

}
