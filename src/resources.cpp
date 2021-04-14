#include "common.h"

#include "resources.h"

// TODO: Handle more gracefully.
// Also consider accepting two sets of flags, required and optional ones.
static uint32_t SelectMemoryType(const VkPhysicalDeviceMemoryProperties& memory_properties, uint32_t memory_type_bits,
		VkMemoryPropertyFlags flags)
{
	for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i)
	{
		if (((memory_type_bits & (1 << i)) != 0) && ((memory_properties.memoryTypes[i].propertyFlags & flags) == flags))
		{
			return i;
		}
	}

	printf("ERROR: No compatible memory type found.\n");
	assert(false);
	return ~0u;
}

void CreateBuffer(Buffer& result, VkDevice device, const VkPhysicalDeviceMemoryProperties& memory_properties,
		size_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_flags)
{
	VkBufferCreateInfo create_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	// create_info.flags;
	create_info.size = size;
	create_info.usage = usage;
	// create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VkBuffer buffer = VK_NULL_HANDLE;
	VK_CHECK(vkCreateBuffer(device, &create_info, nullptr, &buffer));
	assert(buffer);

	VkMemoryRequirements requirements;
	vkGetBufferMemoryRequirements(device, buffer, &requirements);

	const uint32_t memory_type = SelectMemoryType(memory_properties, requirements.memoryTypeBits, memory_flags);
	// const uint32_t memory_type = SelectMemoryType(memory_properties, requirements.memoryTypeBits,
	//		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	// Potential flags:
	//  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT = 0x00000001,
	//  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT = 0x00000002,
	//  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT = 0x00000004,
	//  VK_MEMORY_PROPERTY_HOST_CACHED_BIT = 0x00000008,
	//  VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT = 0x00000010,
	//  VK_MEMORY_PROPERTY_PROTECTED_BIT = 0x00000020,
	//  VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD = 0x00000040,
	//  VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD = 0x00000080,

	VkMemoryAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	alloc_info.allocationSize = requirements.size;
	alloc_info.memoryTypeIndex = memory_type;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VK_CHECK(vkAllocateMemory(device, &alloc_info, nullptr, &memory));
	assert(memory);

	VK_CHECK(vkBindBufferMemory(device, buffer, memory, 0));

	void* data = nullptr;
	if (memory_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
	{
		// TODO: Do we ever unmap?
		VK_CHECK(vkMapMemory(device, memory, 0, size, 0, &data));
	}
	// I think Areseny mentioned something along the lines: "host visible + coherent is similar to OpenGL's persistent
	// (+ coherent?)".

	result.buffer = buffer;
	result.memory = memory;
	result.size = size;
	result.data = data;
}

void UploadBuffer(VkDevice device, VkCommandPool cmd_pool, VkCommandBuffer cmd_buf, VkQueue queue, const Buffer& buffer,
		const Buffer& scratch, const void* data, size_t size)
{
	// TODO: This is submitting a command buffer and waiting for device idle, batch this.
	assert(scratch.data);
	assert(scratch.size >= size);
	memcpy(scratch.data, data, size);

	VK_CHECK(vkResetCommandPool(device, cmd_pool, 0));

	VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	VK_CHECK(vkBeginCommandBuffer(cmd_buf, &begin_info));

	VkBufferCopy region = { 0, 0, VkDeviceSize(size) };
	vkCmdCopyBuffer(cmd_buf, scratch.buffer, buffer.buffer, 1, &region);

	VkBufferMemoryBarrier copy_barrier =
			BufferBarrier(buffer.buffer, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
	vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 1, &copy_barrier, 0, nullptr);

	VK_CHECK(vkEndCommandBuffer(cmd_buf));

	VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
	submit_info.pCommandBuffers = &cmd_buf;
	submit_info.commandBufferCount = 1;
	VK_CHECK(vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE));
	VK_CHECK(vkDeviceWaitIdle(device));
}

void DestroyBuffer(const Buffer& buffer, VkDevice device)
{
	// No need to unmap.
	vkFreeMemory(device, buffer.memory, nullptr);
	vkDestroyBuffer(device, buffer.buffer, nullptr);
}

static VkImageView
CreateImageView(VkDevice device, VkImage image, VkFormat format, uint32_t mip_level, uint32_t level_count)
{
	assert(device);
	assert(image);
	assert(format);

	VkImageAspectFlags aspect_mask =
			(format == VK_FORMAT_D32_SFLOAT) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;


	VkImageViewCreateInfo img_view_create_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	img_view_create_info.image = image;
	img_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	img_view_create_info.format = format;
	img_view_create_info.subresourceRange.aspectMask = aspect_mask;
	img_view_create_info.subresourceRange.baseMipLevel = mip_level;
	img_view_create_info.subresourceRange.levelCount = level_count;
	img_view_create_info.subresourceRange.layerCount = 1;

	VkImageView img_view = VK_NULL_HANDLE;
	VK_CHECK(vkCreateImageView(device, &img_view_create_info, nullptr, &img_view));
	return img_view;
}


Image CreateImage(VkDevice device, const VkPhysicalDeviceMemoryProperties& memory_properties, uint32_t width,
		uint32_t height, uint32_t mip_levels, VkFormat format, VkImageUsageFlags usage)
{
	VkImageCreateInfo img_create_info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	img_create_info.imageType = VK_IMAGE_TYPE_2D;
	img_create_info.format = format;
	img_create_info.extent = { width, height, 1 };
	img_create_info.mipLevels = mip_levels;
	img_create_info.arrayLayers = 1;
	img_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
	img_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	img_create_info.usage = usage;
	img_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VkImage image = VK_NULL_HANDLE;
	VK_CHECK(vkCreateImage(device, &img_create_info, nullptr, &image));

	VkMemoryRequirements requirements;
	vkGetImageMemoryRequirements(device, image, &requirements);

	const uint32_t memory_type_index =
			SelectMemoryType(memory_properties, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	assert(memory_type_index != ~0u);

	VkMemoryAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	alloc_info.allocationSize = requirements.size;
	alloc_info.memoryTypeIndex = memory_type_index;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VK_CHECK(vkAllocateMemory(device, &alloc_info, nullptr, &memory));
	assert(memory);

	VK_CHECK(vkBindImageMemory(device, image, memory, 0));

	Image result;
	result.image = image;
	result.image_view = CreateImageView(device, image, format, 0, mip_levels);
	result.memory = memory;
	return result;
}

void DestroyImage(VkDevice device, Image image)
{
	vkDestroyImageView(device, image.image_view, nullptr);
	vkDestroyImage(device, image.image, nullptr);
	vkFreeMemory(device, image.memory, nullptr);
}

VkImageMemoryBarrier ImageBarrier(VkImage image, VkAccessFlags src_access_mask, VkAccessFlags dst_access_mask,
		VkImageLayout old_layout, VkImageLayout new_layout, VkImageAspectFlags aspect_mask)
{
	VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	barrier.srcAccessMask = src_access_mask;
	barrier.dstAccessMask = dst_access_mask;
	barrier.oldLayout = old_layout;
	barrier.newLayout = new_layout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = aspect_mask;
	// barrier.subresourceRange.baseMipLevel;
	barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;    // NOTE: Some Android driver ignore this.
																	  // barrier.subresourceRange.baseArrayLayer;
	barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;  // NOTE: Some Android driver ignore this.

	return barrier;
}

VkBufferMemoryBarrier BufferBarrier(VkBuffer buffer, VkAccessFlags src_access_mask, VkAccessFlags dst_access_mask)
{
	VkBufferMemoryBarrier barrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
	barrier.srcAccessMask = src_access_mask;
	barrier.dstAccessMask = dst_access_mask;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.buffer = buffer;
	barrier.offset = 0;
	barrier.size = VK_WHOLE_SIZE;  // Broken on some Android devices.
	return barrier;
}
