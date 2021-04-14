#pragma once

struct Buffer
{
	VkBuffer buffer;
	VkDeviceMemory memory;
	void* data;
	size_t size;
};

void CreateBuffer(Buffer& result, VkDevice device, const VkPhysicalDeviceMemoryProperties& memory_properties,
		size_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_flags);
void UploadBuffer(VkDevice device, VkCommandPool cmd_pool, VkCommandBuffer cmd_buf, VkQueue queue, const Buffer& buffer,
		const Buffer& scratch, const void* data, size_t size);
void DestroyBuffer(const Buffer& buffer, VkDevice device);

struct Image
{
	VkImage image;
	VkImageView image_view;
	VkDeviceMemory memory;
};

Image CreateImage(VkDevice device, const VkPhysicalDeviceMemoryProperties& memory_properties, uint32_t width,
		uint32_t height, uint32_t mip_levels, VkFormat format, VkImageUsageFlags usage);
void DestroyImage(VkDevice device, Image image);

VkImageMemoryBarrier ImageBarrier(VkImage image, VkAccessFlags src_access_mask, VkAccessFlags dst_access_mask,
		VkImageLayout old_layout, VkImageLayout new_layout, VkImageAspectFlags aspect_mask);
VkBufferMemoryBarrier BufferBarrier(VkBuffer buffer, VkAccessFlags src_access_mask, VkAccessFlags dst_access_mask);
