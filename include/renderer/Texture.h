#pragma once
#include "renderer/VulkanBackend.h"
#include "renderer/RenderTypes.h"

struct TextureView
{
	Texture* texture = nullptr;
	VkImageView view = VK_NULL_HANDLE;

	TextureFormat format = TEXTURE_FORMAT_UNDEFINED;
	DescriptorAllocation descriptor;

	TextureViewCreateInfo create_info;

	void WriteDescriptorInfo(VkDescriptorType type, VkImageLayout layout, uint32_t descriptor_offset = 0);
	void TransitionLayout(VkCommandBuffer command_buffer, VkImageLayout new_layout) const;
	void TransitionLayoutImmediate(VkImageLayout new_layout) const;
	VkImageLayout GetLayout() const;
};

class Texture
{
public:
	// Factory patterns will return a unique_ptr, which can be converted into a shared_ptr if necessary
	// Factories should never own the memory themselves, and we can't make assumptions of the memory ownership
	// that the caller has intended, so we will return a unique_ptr
	static std::unique_ptr<Texture> Create(const TextureCreateInfo& create_info);

public:
	Texture(const TextureCreateInfo& create_info);
	~Texture();

public:
	void GenerateMips();

	void CopyFromBuffer(VkCommandBuffer command_buffer, const Buffer& src_buffer, VkDeviceSize src_offset = 0) const;
	void CopyFromBufferImmediate(const Buffer& src_buffer, VkDeviceSize src_offset = 0) const;

	void TransitionLayout(VkCommandBuffer command_buffer, VkImageLayout new_layout,
		uint32_t base_mip = 0, uint32_t num_mips = UINT32_MAX, uint32_t base_layer = 0, uint32_t num_layers = UINT32_MAX) const;
	void TransitionLayoutImmediate(VkImageLayout new_layout,
		uint32_t base_mip = 0, uint32_t num_mips = UINT32_MAX, uint32_t base_layer = 0, uint32_t num_layers = UINT32_MAX) const;

	void AppendToChain(std::unique_ptr<Texture>&& texture);
	Texture& GetChainned(uint32_t index = 0);

	TextureView* GetView(const TextureViewCreateInfo& view_info = {});
	// NOTE: Should probably be removed
	VkImage GetVkImage() const { return m_vk_image; }

private:
	VkImage m_vk_image = VK_NULL_HANDLE;
	VkDeviceMemory m_vk_device_memory = VK_NULL_HANDLE;

	TextureCreateInfo m_create_info;

	std::unordered_map<TextureViewCreateInfo, TextureView> m_views;
	std::vector<std::unique_ptr<Texture>> m_chainned_textures;

};
