#pragma once
#include "renderer/VulkanBackend.h"
#include "renderer/RenderTypes.h"

struct TextureViewCreateInfo
{
	VkImageViewType type = VK_IMAGE_VIEW_TYPE_MAX_ENUM;

	uint32_t base_mip = 0;
	uint32_t num_mips = UINT32_MAX;
	uint32_t base_layer = 0;
	uint32_t num_layers = UINT32_MAX;

	bool operator==(const TextureViewCreateInfo& other) const
	{
		return (
			type == other.type &&
			base_mip == other.base_mip &&
			num_mips == other.num_mips &&
			base_layer == other.base_layer &&
			num_layers == other.num_layers
		);
	}
};

template<>
struct std::hash<TextureViewCreateInfo>
{
	std::size_t operator()(const TextureViewCreateInfo& view_info) const
	{
		return (std::hash<uint32_t>()(view_info.type) ^
			(std::hash<uint32_t>()(view_info.base_mip) << 12) ^
			(std::hash<uint32_t>()(view_info.num_mips) << 24) ^
			(std::hash<uint32_t>()(view_info.base_layer) << 36) ^
			(std::hash<uint32_t>()(view_info.num_layers) << 48)
		);
	}
};

struct TextureView
{
	Texture* texture = nullptr;

	VkImageView view = VK_NULL_HANDLE;
	VkFormat format = VK_FORMAT_UNDEFINED;
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
	// of the callee, so we will return a unique_ptr
	static std::unique_ptr<Texture> Create(const TextureCreateInfo& create_info);

public:
	Texture(const TextureCreateInfo& create_info);
	~Texture();

public:
	void CopyFromBuffer(VkCommandBuffer command_buffer, const Buffer& src_buffer, VkDeviceSize src_offset = 0) const;
	void CopyFromBufferImmediate(const Buffer& src_buffer, VkDeviceSize src_offset = 0) const;

	void TransitionLayout(VkCommandBuffer command_buffer, VkImageLayout new_layout,
		uint32_t base_mip = 0, uint32_t num_mips = UINT32_MAX, uint32_t base_layer = 0, uint32_t num_layers = UINT32_MAX) const;
	void TransitionLayoutImmediate(VkImageLayout new_layout,
		uint32_t base_mip = 0, uint32_t num_mips = UINT32_MAX, uint32_t base_layer = 0, uint32_t num_layers = UINT32_MAX) const;

	void AppendToChain(std::unique_ptr<Texture>&& texture);
	Texture& GetChainned(uint32_t index = 0);

	TextureView* GetView(const TextureViewCreateInfo& view_info = {});
	VkImage GetVkImage() const { return m_vk_image; }
	VkFormat GetVkFormat() const;

private:
	VkImage m_vk_image = VK_NULL_HANDLE;
	VkDeviceMemory m_vk_device_memory = VK_NULL_HANDLE;

	TextureCreateInfo m_create_info;

	std::unordered_map<TextureViewCreateInfo, TextureView> m_views;
	std::vector<std::unique_ptr<Texture>> m_chainned_textures;

};
