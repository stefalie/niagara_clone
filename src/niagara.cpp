// Disable warnings for unscoped enums, the Vulkan headers are full of them.
#pragma warning(disable : 26812)

// Uninitialized members
#pragma warning(disable : 26495)

#include <assert.h>
#include <stdio.h>

#include <algorithm>
#include <vector>

#include <fast_obj.h>
#include <meshoptimizer.h>
#include <volk.h>
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

// SHORTCUT: Would need to be checked properly in production.
#define VK_CHECK(call)            \
	do                            \
	{                             \
		const VkResult rc = call; \
		assert(rc == VK_SUCCESS); \
	} while (0)

#ifndef ARRAYSIZE
#define ARRAYSIZE(array) (sizeof(array) / sizeof((array)[0]))
#endif

VkInstance CreateInstance();
VkDebugReportCallbackEXT RegisterDebugCallback(VkInstance instance);
VkDebugUtilsMessengerEXT RegisterDebugUtilsMessenger(VkInstance instance);
// TODO remove here from pubic api
uint32_t GetGraphicsFamilyIndex(VkPhysicalDevice physical_device);
VkPhysicalDevice PickPhysicalDevice(VkInstance instance);
VkDevice CreateDevice(VkInstance instance, VkPhysicalDevice physical_device, uint32_t family_index);
VkSurfaceKHR CreateSurface(VkInstance instance, GLFWwindow* window);
VkFormat GetSwapchainFormat(VkPhysicalDevice physical_device, VkSurfaceKHR surface);
VkSemaphore CreateSemaphore(VkDevice device);
VkRenderPass CreateRenderPass(VkDevice device, VkFormat format);
VkFramebuffer
CreateFrameBuffer(VkDevice device, VkRenderPass render_pass, VkImageView image_view, uint32_t width, uint32_t height);
VkImageView CreateImageView(VkDevice device, VkImage image, VkFormat format);
VkShaderModule LoadShader(VkDevice device, const char* path);
VkDescriptorSetLayout CreateDescriptorSetLayout(VkDevice device);
VkPipelineLayout CreatePipelineLayout(VkDevice device, VkDescriptorSetLayout set_layout);
VkPipeline CreateGraphicsPipeline(VkDevice device, VkPipelineCache pipeline_cache, VkRenderPass render_pass,
		VkPipelineLayout layout, VkShaderModule vert, VkShaderModule frag);
VkCommandPool CreateCommandBufferPool(VkDevice device, uint32_t family_index);
VkImageMemoryBarrier ImageBarrier(VkImage image, VkAccessFlags src_access_mask, VkAccessFlags dst_access_mask,
		VkImageLayout old_layout, VkImageLayout new_layout);

struct Vertex
{
	float vx, vy, vz;
	// float nx, ny, nz;
	uint8_t nx, ny, nz, nw;
	float tu, tv;
};
struct Mesh
{
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
};

bool LoadMesh(Mesh& result, const char* path)
{
	fastObjMesh* obj = fast_obj_read(path);
	if (!obj)
	{
		return false;
	}

	size_t index_count = 0;
	for (size_t i = 0; i < obj->face_count; ++i)
	{
		index_count += 3ull * (obj->face_vertices[i] - 2);
	}

	std::vector<Vertex> vertices(index_count);

	size_t vertex_offset = 0;
	size_t index_offset = 0;

	for (size_t i = 0; i < obj->face_count; ++i)
	{
		for (size_t j = 0; j < obj->face_vertices[i]; ++j)
		{
			fastObjIndex idx = obj->indices[index_offset + j];

			// Triangulize on the fly, works only for Convex faces.
			if (j >= 3)
			{
				vertices[vertex_offset + 0] = vertices[vertex_offset - 3];
				vertices[vertex_offset + 1] = vertices[vertex_offset - 1];
				vertex_offset += 2;
			}

			Vertex& v = vertices[vertex_offset++];


			float nx = obj->normals[idx.n * 3 + 0];
			float ny = obj->normals[idx.n * 3 + 1];
			float nz = obj->normals[idx.n * 3 + 2];

			v.vx = obj->positions[idx.p * 3 + 0];
			v.vy = obj->positions[idx.p * 3 + 1];
			v.vz = obj->positions[idx.p * 3 + 2];
			// TODO: Fix rounding.
			v.nx = uint8_t(nx * 127.0f + 127.0f);
			v.ny = uint8_t(ny * 127.0f + 127.0f);
			v.nz = uint8_t(nz * 127.0f + 127.0f);
			v.tu = obj->texcoords[idx.t * 3 + 0];
			v.tv = obj->texcoords[idx.t * 3 + 1];
		}

		index_offset += obj->face_vertices[i];
	}
	assert(vertex_offset == index_count);

	fast_obj_destroy(obj);

	const bool kUseIndices = true;
	if (!kUseIndices)  // No indexing.
	{
		result.vertices = vertices;
		result.indices.resize(index_count);

		for (uint32_t i = 0; i < index_count; ++i)
		{
			result.indices[i] = i;
		}
	}
	else
	{
		// Make index buffer.
		std::vector<uint32_t> remap(index_count);
		size_t unique_vertices_count = meshopt_generateVertexRemap(
				remap.data(), nullptr, index_count, vertices.data(), index_count, sizeof(Vertex));

		result.vertices.resize(unique_vertices_count);
		result.indices.resize(index_count);

		meshopt_remapVertexBuffer(result.vertices.data(), vertices.data(), index_count, sizeof(Vertex), remap.data());
		meshopt_remapIndexBuffer(result.indices.data(), nullptr, index_count, remap.data());

		// TODO: Optimize mesh for more efficient GPU rendering.
	}

	return true;
}

struct Buffer
{
	VkBuffer buffer;
	VkDeviceMemory memory;
	void* data;
	size_t size;
};

// TODO: Handle more gracefully.
// Also consider accepting two sets of flags, required and optional ones.
uint32_t SelectMemoryType(const VkPhysicalDeviceMemoryProperties& memory_properties, uint32_t memory_type_bits,
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
		size_t size, VkBufferUsageFlags usage)
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

	const uint32_t memory_type = SelectMemoryType(memory_properties, requirements.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

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
	VK_CHECK(vkMapMemory(device, memory, 0, size, 0, &data));

	result.buffer = buffer;
	result.memory = memory;
	result.size = size;
	result.data = data;
}

void DestroyBuffer(const Buffer& buffer, VkDevice device)
{
	// No need to unmap.
	vkFreeMemory(device, buffer.memory, nullptr);
	vkDestroyBuffer(device, buffer.buffer, nullptr);
}

struct Swapchain
{
	VkSwapchainKHR swapchain;

	std::vector<VkImage> images;
	std::vector<VkImageView> image_views;
	std::vector<VkFramebuffer> framebuffers;

	uint32_t width;
	uint32_t height;
	uint32_t image_count;  // TODO: Redundant
};

VkSwapchainKHR CreateSwapchain(VkDevice device, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR surface_caps,
		VkFormat format, uint32_t family_index, uint32_t width, uint32_t height, VkSwapchainKHR old_swapchain);
// TODO: Make descriptor for all the inputs ...
void CreateSwapchain(VkPhysicalDevice phsyical_device, VkDevice device, VkSurfaceKHR surface, VkFormat format,
		uint32_t family_index, VkRenderPass render_pass, VkSwapchainKHR old_swapchain, Swapchain& result);
void ResizeSwapchainIfNecessary(VkPhysicalDevice physical_device, VkDevice device, VkSurfaceKHR surface,
		VkFormat format, uint32_t family_index, VkRenderPass render_pass, Swapchain& result);
void DestroySwapchain(VkDevice device, const Swapchain& swapchain);

int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		printf("Usage: %s [mesh]\n", argv[0]);
		return 1;
	}

	const int rc = glfwInit();
	assert(rc == 1);

	VK_CHECK(volkInitialize());

	VkInstance instance = CreateInstance();
	assert(instance);

	volkLoadInstance(instance);

#ifdef _DEBUG
	VkDebugReportCallbackEXT debug_callback = RegisterDebugCallback(instance);
	assert(debug_callback);
	VkDebugUtilsMessengerEXT debug_messenger = RegisterDebugUtilsMessenger(instance);
	assert(debug_messenger);
#endif

	VkPhysicalDevice physical_device = PickPhysicalDevice(instance);
	assert(physical_device);

	const uint32_t family_index = GetGraphicsFamilyIndex(physical_device);
	assert(family_index != VK_QUEUE_FAMILY_IGNORED);

	VkDevice device = CreateDevice(instance, physical_device, family_index);
	assert(device);

	const int window_width = 1024;
	const int window_height = 768;
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // For NVidia
	GLFWwindow* window = glfwCreateWindow(window_width, window_height, "Hello Vulkan", nullptr, nullptr);
	assert(window);

	VkSurfaceKHR surface = CreateSurface(instance, window);
	assert(surface);

	// NOTE: It's stupid that you need to first open a windows and create a surface just to see if its supported.
	// TODO: I guess this should happen as part of the family picking.
	// The order is then:
	// 1. Create instance
	// 2. Open window
	// 3. Create surface
	// (the family index picking can now pick a family that supports the surface)
	// 4. Pick physical device with the help of instance, surface
	// 5. Call GetGraphicsFamilyIndex again
	VkBool32 is_surface_supported = false;
	VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, family_index, surface, &is_surface_supported));
	assert(is_surface_supported);

	VkFormat swapchain_format = GetSwapchainFormat(physical_device, surface);
	assert(swapchain_format);

	//// TODO move: into swapchain creation. Or keep it here as it will be reused upon resizing.
	// VkSurfaceCapabilitiesKHR surface_caps;
	// VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &surface_caps));

	VkSemaphore aquire_semaphore = CreateSemaphore(device);
	assert(aquire_semaphore);
	VkSemaphore release_semaphore = CreateSemaphore(device);
	assert(release_semaphore);

	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(device, family_index, 0, &queue);
	assert(queue);

	VkRenderPass render_pass = CreateRenderPass(device, swapchain_format);
	assert(render_pass);

	// NOTE: This is earlier here than what Arseny is doing.
	Swapchain swapchain;
	CreateSwapchain(
			physical_device, device, surface, swapchain_format, family_index, render_pass, VK_NULL_HANDLE, swapchain);

	VkShaderModule triangle_vert = LoadShader(device, "triangle.vert.spv");
	VkShaderModule triangle_frag = LoadShader(device, "triangle.frag.spv");

	// TODO: Critical for perf.
	VkPipelineCache pipeline_cache = VK_NULL_HANDLE;


	VkDescriptorSetLayout set_layout = CreateDescriptorSetLayout(device);
	assert(set_layout);

	VkPipelineLayout pipeline_layout = CreatePipelineLayout(device, set_layout);

	VkPipeline triangle_pipeline =
			CreateGraphicsPipeline(device, pipeline_cache, render_pass, pipeline_layout, triangle_vert, triangle_frag);
	assert(triangle_pipeline);

	VkCommandPool cmd_buf_pool = CreateCommandBufferPool(device, family_index);
	assert(cmd_buf_pool);

	VkCommandBufferAllocateInfo cmd_buf_alloc_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	cmd_buf_alloc_info.commandPool = cmd_buf_pool;
	cmd_buf_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmd_buf_alloc_info.commandBufferCount = 1;

	VkCommandBuffer cmd_buf = VK_NULL_HANDLE;
	VK_CHECK(vkAllocateCommandBuffers(device, &cmd_buf_alloc_info, &cmd_buf));

	VkPhysicalDeviceMemoryProperties memory_properties;
	vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

	Mesh mesh;
	const bool mesh_rc = LoadMesh(mesh, argv[1]);
	assert(mesh_rc);

	Buffer vertex_buffer = {};
	// CreateBuffer(vertex_buffer, device, memory_properties, 128 * 1024 * 1024, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
	CreateBuffer(vertex_buffer, device, memory_properties, 128 * 1024 * 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
	Buffer index_buffer = {};
	CreateBuffer(index_buffer, device, memory_properties, 128 * 1024 * 1024, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

	assert(vertex_buffer.size >= mesh.vertices.size() * sizeof(Vertex));
	assert(index_buffer.size >= mesh.indices.size() * sizeof(uint32_t));
	memcpy(vertex_buffer.data, mesh.vertices.data(), mesh.vertices.size() * sizeof(Vertex));
	memcpy(index_buffer.data, mesh.indices.data(), mesh.indices.size() * sizeof(uint32_t));

	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();
		if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
		{
			glfwSetWindowShouldClose(window, GLFW_TRUE);
		}

		ResizeSwapchainIfNecessary(
				physical_device, device, surface, swapchain_format, family_index, render_pass, swapchain);
		//// Let's use glfw for this for now.
		// int curr_fb_width, curr_fb_height;
		// glfwGetFramebufferSize(window, &curr_fb_width, &curr_fb_height);
		// if (curr_fb_width != fb_width || curr_fb_height != fb_height)
		//{
		//	fb_width = curr_fb_width;
		//	fb_height = curr_fb_height;
		//	ResizeSwapchain(device, surface, surface_caps, swapchain_format, family_index, render_pass, fb_width,
		// fb_height, swapchain);
		//}
		//// TODO: Could we just use the glfw resize callback?.

		uint32_t image_index = 0;
		VK_CHECK(vkAcquireNextImageKHR(
				device, swapchain.swapchain, ~0ull, aquire_semaphore, VK_NULL_HANDLE, &image_index));

		VK_CHECK(vkResetCommandPool(device, cmd_buf_pool, 0));

		VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		VK_CHECK(vkBeginCommandBuffer(cmd_buf, &begin_info));

		// NOTE: Likely a VKSubpassDependency could be used here instead of the barrier. This is explained in:
		// https://themaister.net/blog/2019/08/14/yet-another-blog-explaining-vulkan-synchronization/
		VkImageMemoryBarrier render_begin_barrier =
				ImageBarrier(swapchain.images[image_index], 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
						VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 0, nullptr, 1,
				&render_begin_barrier);

		VkClearColorValue clear_color = { 48.0f / 255.0f, 10.0f / 255.0f, 36.0f / 255.0f,
			1.0f };	 // Ubuntu terminal color.
		VkClearValue clear_value = { clear_color };

		VkRenderPassBeginInfo pass_begin_info = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
		pass_begin_info.renderPass = render_pass;
		pass_begin_info.framebuffer = swapchain.framebuffers[image_index];
		pass_begin_info.renderArea.extent.width = swapchain.width;
		pass_begin_info.renderArea.extent.height = swapchain.height;
		pass_begin_info.clearValueCount = 1;
		pass_begin_info.pClearValues = &clear_value;

		vkCmdBeginRenderPass(cmd_buf, &pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport viewport = { 0.0f, (float)swapchain.height, (float)swapchain.width, -(float)swapchain.height, 0.0f,
			1.0f };
		VkRect2D scissor = { { 0, 0 }, { (uint32_t)swapchain.width, (uint32_t)swapchain.height } };

		vkCmdSetViewport(cmd_buf, 0, 1, &viewport);
		vkCmdSetScissor(cmd_buf, 0, 1, &scissor);

		vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, triangle_pipeline);


		// Version with vertex buffers.
		// VkDeviceSize dummy_offset = 0;
		// vkCmdBindVertexBuffers(cmd_buf, 0, 1, &vertex_buffer.buffer, &dummy_offset);

		{
			// Descriptor set binding is a good match for AMD, but not for NVidia (and likely neither for Intel).
			// We won't use descriptor set binding, we'll use an extension exposes by Intel and NVidia only.
			// They are like push constants, but for descriptor sets.
			VkDescriptorBufferInfo buffer_info = {};
			buffer_info.buffer = vertex_buffer.buffer;
			buffer_info.offset = 0;
			buffer_info.range = vertex_buffer.size;

			VkWriteDescriptorSet descriptors[1] = {};
			descriptors[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;	// Why different here?
			// I guess we skip this because we push and don't have to allocate a set from a pool.
			// descriptors[0].dstSet = ?;
			descriptors[0].dstBinding = 0;
			descriptors[0].descriptorCount = 1;
			descriptors[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			descriptors[0].pBufferInfo = &buffer_info;
			vkCmdPushDescriptorSetKHR(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, descriptors);
		}


		vkCmdBindIndexBuffer(cmd_buf, index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(cmd_buf, (uint32_t)mesh.indices.size(), 1, 0, 0, 0);
		// vkCmdDraw(cmd_buf, 3, 1, 0, 0);


		vkCmdEndRenderPass(cmd_buf);

		// VkClearColorValue color = { 1, 0, 1, 1 };
		// VkImageSubresourceRange range = {};
		// range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		// range.levelCount = 1;
		// range.layerCount = 1;
		// vkCmdClearColorImage(cmd_buf, swapchain_images[image_index], VK_IMAGE_LAYOUT_GENERAL, &color, 1, &range);

		VkImageMemoryBarrier render_end_barrier =
				ImageBarrier(swapchain.images[image_index], VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0,
						VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
		// TODO: used to be VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
		vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 0, nullptr, 1,
				&render_end_barrier);

		VK_CHECK(vkEndCommandBuffer(cmd_buf));

		VkPipelineStageFlags submit_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

		VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
		submit_info.waitSemaphoreCount = 1;
		submit_info.pWaitSemaphores = &aquire_semaphore;
		submit_info.pWaitDstStageMask = &submit_stage_mask;
		submit_info.pCommandBuffers = &cmd_buf;
		submit_info.commandBufferCount = 1;
		submit_info.signalSemaphoreCount = 1;
		submit_info.pSignalSemaphores = &release_semaphore;
		VK_CHECK(vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE));

		VkPresentInfoKHR present_info = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
		present_info.waitSemaphoreCount = 1;
		present_info.pWaitSemaphores = &release_semaphore;
		present_info.swapchainCount = 1;
		present_info.pSwapchains = &swapchain.swapchain;
		present_info.pImageIndices = &image_index;

		VK_CHECK(vkQueuePresentKHR(queue, &present_info));

		// TODO go away
		VK_CHECK(vkDeviceWaitIdle(device));
	}

	VK_CHECK(vkDeviceWaitIdle(device));

	DestroyBuffer(vertex_buffer, device);
	DestroyBuffer(index_buffer, device);

	vkDestroyCommandPool(device, cmd_buf_pool, nullptr);

	vkDestroyPipeline(device, triangle_pipeline, nullptr);
	vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
	// vkDestroyPipelineCache(device, pipeline_cache, nullptr);

	vkDestroyDescriptorSetLayout(device, set_layout, nullptr);

	vkDestroyShaderModule(device, triangle_frag, nullptr);
	vkDestroyShaderModule(device, triangle_vert, nullptr);

	DestroySwapchain(device, swapchain);

	vkDestroyRenderPass(device, render_pass, nullptr);

	vkDestroySemaphore(device, release_semaphore, nullptr);
	vkDestroySemaphore(device, aquire_semaphore, nullptr);

	vkDestroySurfaceKHR(instance, surface, nullptr);

	glfwDestroyWindow(window);

	vkDestroyDevice(device, nullptr);

#ifdef _DEBUG
	vkDestroyDebugReportCallbackEXT(instance, debug_callback, nullptr);
	vkDestroyDebugUtilsMessengerEXT(instance, debug_messenger, nullptr);
#endif
	vkDestroyInstance(instance, nullptr);

	return 0;
}

VkInstance CreateInstance()
{
	// SHORTCUT: Check if version is available via vkEnumerateInstanceVersion()
	VkApplicationInfo app_info = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
	app_info.apiVersion = VK_API_VERSION_1_2;
	app_info.pApplicationName = "Niagara Clone Test";

	VkInstanceCreateInfo instance_create_info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	instance_create_info.pApplicationInfo = &app_info;

	// SHORTCUT: Check availability of theses?
	char const* const debug_layers[] = {
		"VK_LAYER_KHRONOS_validation",
		// TODO: What's the difference? I believe above supersedes this.
		//"VK_LAYER_LUNARG_standard_validation",
		// TODO: Figure out how to use that.
		// "VK_LAYER_RENDERDOC_Capture",

	};
#ifdef _DEBUG
	instance_create_info.ppEnabledLayerNames = debug_layers;
	instance_create_info.enabledLayerCount = ARRAYSIZE(debug_layers);
#endif

	char const* const extensions[] = {
		VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef VK_USE_PLATFORM_WIN32_KHR
		VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif
#ifdef _DEBUG
		VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
		VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#endif
	};
	instance_create_info.ppEnabledExtensionNames = extensions;
	instance_create_info.enabledExtensionCount = ARRAYSIZE(extensions);

	VkInstance instance = VK_NULL_HANDLE;
	VK_CHECK(vkCreateInstance(&instance_create_info, nullptr, &instance));

	return instance;
}

static VkBool32 DebugReportCallback(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object,
		size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage, void* pUserData)
{
	// HACK, TODO, unclear if our bug or vulkan bug
	// if (strstr(pMessage,
	//			"Shader requires VkPhysicalDeviceFloat16Int8FeaturesKHR::shaderInt8 but is not enabled on the device"))
	//{
	//	return VK_FALSE;
	//}

	if ((flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT) || (flags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT))
	{
		return VK_FALSE;
	}

	const char* type = (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT)										  ? "ERROR" :
			(flags & (VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT)) ? "WARNING" :
			(flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT)													  ? "DEBUG" :
																										  "INFO";

	char message[4096];
	snprintf(message, ARRAYSIZE(message), "%s: %s\n\n", type, pMessage);

	printf(message);
#ifdef _WIN32
	OutputDebugStringA(message);
#endif

	if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
	{
		assert(!"Validation error!");
	}
	return VK_FALSE;
}

static inline const char* GetVkObjectType(VkObjectType input_value)
{
	switch (input_value)
	{
#define CASE(type) \
	case type:     \
		return #type
		CASE(VK_OBJECT_TYPE_UNKNOWN);
		CASE(VK_OBJECT_TYPE_INSTANCE);
		CASE(VK_OBJECT_TYPE_PHYSICAL_DEVICE);
		CASE(VK_OBJECT_TYPE_DEVICE);
		CASE(VK_OBJECT_TYPE_QUEUE);
		CASE(VK_OBJECT_TYPE_SEMAPHORE);
		CASE(VK_OBJECT_TYPE_COMMAND_BUFFER);
		CASE(VK_OBJECT_TYPE_FENCE);
		CASE(VK_OBJECT_TYPE_DEVICE_MEMORY);
		CASE(VK_OBJECT_TYPE_BUFFER);
		CASE(VK_OBJECT_TYPE_IMAGE);
		CASE(VK_OBJECT_TYPE_EVENT);
		CASE(VK_OBJECT_TYPE_QUERY_POOL);
		CASE(VK_OBJECT_TYPE_BUFFER_VIEW);
		CASE(VK_OBJECT_TYPE_IMAGE_VIEW);
		CASE(VK_OBJECT_TYPE_SHADER_MODULE);
		CASE(VK_OBJECT_TYPE_PIPELINE_CACHE);
		CASE(VK_OBJECT_TYPE_PIPELINE_LAYOUT);
		CASE(VK_OBJECT_TYPE_RENDER_PASS);
		CASE(VK_OBJECT_TYPE_PIPELINE);
		CASE(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT);
		CASE(VK_OBJECT_TYPE_SAMPLER);
		CASE(VK_OBJECT_TYPE_DESCRIPTOR_POOL);
		CASE(VK_OBJECT_TYPE_DESCRIPTOR_SET);
		CASE(VK_OBJECT_TYPE_FRAMEBUFFER);
		CASE(VK_OBJECT_TYPE_COMMAND_POOL);
		CASE(VK_OBJECT_TYPE_SAMPLER_YCBCR_CONVERSION);
		CASE(VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE);
		CASE(VK_OBJECT_TYPE_SURFACE_KHR);
		CASE(VK_OBJECT_TYPE_SWAPCHAIN_KHR);
		CASE(VK_OBJECT_TYPE_DISPLAY_KHR);
		CASE(VK_OBJECT_TYPE_DISPLAY_MODE_KHR);
		CASE(VK_OBJECT_TYPE_DEBUG_REPORT_CALLBACK_EXT);
		CASE(VK_OBJECT_TYPE_DEBUG_UTILS_MESSENGER_EXT);
		CASE(VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR);
		CASE(VK_OBJECT_TYPE_VALIDATION_CACHE_EXT);
		CASE(VK_OBJECT_TYPE_PERFORMANCE_CONFIGURATION_INTEL);
		CASE(VK_OBJECT_TYPE_DEFERRED_OPERATION_KHR);
		CASE(VK_OBJECT_TYPE_INDIRECT_COMMANDS_LAYOUT_NV);
		CASE(VK_OBJECT_TYPE_PRIVATE_DATA_SLOT_EXT);
		// CASE(VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_KHR);
		// CASE(VK_OBJECT_TYPE_SAMPLER_YCBCR_CONVERSION_KHR);
		// CASE(VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_NV);
		CASE(VK_OBJECT_TYPE_MAX_ENUM);
#undef CASE
	default:
		return "Unhandled VkObjectType";
	}
}

static VkBool32 DebugUtilsCallbackLunarG(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
		VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
		void* pUserData)
{
	// HACK, TODO, unclear if our bug or vulkan bug
	// if (strstr(pCallbackData->pMessage,
	//			"Shader requires VkPhysicalDeviceFloat16Int8FeaturesKHR::shaderInt8 but is not enabled on the device"))
	//{
	//	return VK_FALSE;
	//}

	char prefix[64] = "";
	char* message = new char[strlen(pCallbackData->pMessage) + 5000];
	assert(message);
	struct demo* demo = (struct demo*)pUserData;

	if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT)
	{
		strcat(prefix, "VERBOSE : ");
	}
	else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
	{
		strcat(prefix, "INFO : ");
	}
	else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
	{
		strcat(prefix, "WARNING : ");
	}
	else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
	{
		strcat(prefix, "ERROR : ");
	}

	if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT)
	{
		strcat(prefix, "GENERAL");
	}
	else
	{
		if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)
		{
			strcat(prefix, "VALIDATION");
		}
		if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)
		{
			if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)
			{
				strcat(prefix, "|");
			}
			strcat(prefix, "PERFORMANCE");
		}
	}

	sprintf(message, "%s - Message Id Number: %d | Message Id Name: %s\n\t%s\n", prefix, pCallbackData->messageIdNumber,
			pCallbackData->pMessageIdName, pCallbackData->pMessage);
	if (pCallbackData->objectCount > 0)
	{
		char tmp_message[500];
		sprintf(tmp_message, "\n\tObjects - %d\n", pCallbackData->objectCount);
		strcat(message, tmp_message);
		for (uint32_t object = 0; object < pCallbackData->objectCount; ++object)
		{
			if (NULL != pCallbackData->pObjects[object].pObjectName &&
					strlen(pCallbackData->pObjects[object].pObjectName) > 0)
			{
				sprintf(tmp_message, "\t\tObject[%d] - %s, Handle %p, Name \"%s\"\n", object,
						GetVkObjectType(pCallbackData->pObjects[object].objectType),
						(void*)(pCallbackData->pObjects[object].objectHandle),
						pCallbackData->pObjects[object].pObjectName);
			}
			else
			{
				sprintf(tmp_message, "\t\tObject[%d] - %s, Handle %p\n", object,
						GetVkObjectType(pCallbackData->pObjects[object].objectType),
						(void*)(pCallbackData->pObjects[object].objectHandle));
			}
			strcat(message, tmp_message);
		}
	}
	if (pCallbackData->cmdBufLabelCount > 0)
	{
		char tmp_message[500];
		sprintf(tmp_message, "\n\tCommand Buffer Labels - %d\n", pCallbackData->cmdBufLabelCount);
		strcat(message, tmp_message);
		for (uint32_t cmd_buf_label = 0; cmd_buf_label < pCallbackData->cmdBufLabelCount; ++cmd_buf_label)
		{
			sprintf(tmp_message, "\t\tLabel[%d] - %s { %f, %f, %f, %f}\n", cmd_buf_label,
					pCallbackData->pCmdBufLabels[cmd_buf_label].pLabelName,
					pCallbackData->pCmdBufLabels[cmd_buf_label].color[0],
					pCallbackData->pCmdBufLabels[cmd_buf_label].color[1],
					pCallbackData->pCmdBufLabels[cmd_buf_label].color[2],
					pCallbackData->pCmdBufLabels[cmd_buf_label].color[3]);
			strcat(message, tmp_message);
		}
	}

	printf("%s\n", message);
	fflush(stdout);

	delete[] message;
	return false;
}

VkDebugReportCallbackEXT RegisterDebugCallback(VkInstance instance)
{
	assert(instance);

	VkDebugReportCallbackCreateInfoEXT dbg_cb_create_info = { VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT };
	dbg_cb_create_info.flags =
			// VK_DEBUG_REPORT_INFORMATION_BIT_EXT |
			VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT |
			VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_DEBUG_BIT_EXT;
	dbg_cb_create_info.pfnCallback = &DebugReportCallback;
	// dbg_cb_create_info.pUserData;

	VkDebugReportCallbackEXT debug_callback = VK_NULL_HANDLE;
	VK_CHECK(vkCreateDebugReportCallbackEXT(instance, &dbg_cb_create_info, nullptr, &debug_callback));
	return debug_callback;
}

VkDebugUtilsMessengerEXT RegisterDebugUtilsMessenger(VkInstance instance)
{
	VkDebugUtilsMessengerCreateInfoEXT messenger_create_info = {
		VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT
	};
	messenger_create_info.messageSeverity =
			// VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
			// VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	messenger_create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	messenger_create_info.pfnUserCallback = &DebugUtilsCallbackLunarG;
	// messenger_create_info.pUserData = nullptr;  // optional

	VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
	VK_CHECK(vkCreateDebugUtilsMessengerEXT(instance, &messenger_create_info, nullptr, &debug_messenger));
	return debug_messenger;
}

VkBool32 SupportsPresentation(VkPhysicalDevice physical_device, uint32_t family_index)
{
#ifdef VK_USE_PLATFORM_WIN32_KHR
	return vkGetPhysicalDeviceWin32PresentationSupportKHR(physical_device, family_index);
#else
#error Unsupported Platform
#endif
}

uint32_t GetGraphicsFamilyIndex(VkPhysicalDevice physical_device)
{
	uint32_t queue_family_count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, nullptr);

	std::vector<VkQueueFamilyProperties> queue_family_properties(queue_family_count);
	vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_family_properties.data());

	for (uint32_t i = 0; i < queue_family_count; ++i)
	{
		if (queue_family_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
		{
			return i;
		}
	}

	return VK_QUEUE_FAMILY_IGNORED;
}

VkPhysicalDevice PickPhysicalDevice(VkInstance instance)
{
	assert(instance);

	VkPhysicalDevice discrete = VK_NULL_HANDLE;
	VkPhysicalDevice fallback = VK_NULL_HANDLE;

	uint32_t physical_device_count = 0;
	vkEnumeratePhysicalDevices(instance, &physical_device_count, nullptr);

	std::vector<VkPhysicalDevice> physical_devices(physical_device_count);
	vkEnumeratePhysicalDevices(instance, &physical_device_count, physical_devices.data());

	VkPhysicalDevice ret = VK_NULL_HANDLE;


	// Pick 1st discrete GPU.
	for (uint32_t i = 0; i < physical_device_count; ++i)
	{
		VkPhysicalDeviceProperties props;
		vkGetPhysicalDeviceProperties(physical_devices[i], &props);

		printf("GPU %u: %s.\n", i, props.deviceName);

		{  // Check for 8-bit int support.
			// VkPhysicalDeviceProperties2 props2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
			// vkGetPhysicalDeviceProperties2(physical_devices[i], &props2);

			VkPhysicalDeviceFeatures2 features2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
			VkPhysicalDeviceShaderFloat16Int8FeaturesKHR features_f16i8 = {
				VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES_KHR
			};
			features2.pNext = &features_f16i8;
			vkGetPhysicalDeviceFeatures2(physical_devices[i], &features2);

			if (features_f16i8.shaderInt8 != VK_TRUE)
			{
				continue;
			}
			assert(features_f16i8.shaderInt8);
		}

		const uint32_t family_index = GetGraphicsFamilyIndex(physical_devices[i]);
		if (family_index == VK_QUEUE_FAMILY_IGNORED)
		{
			continue;
		}

		if (!SupportsPresentation(physical_devices[i], family_index))
		{
			continue;
		}

		if (props.apiVersion < VK_API_VERSION_1_2)
		{
			continue;
		}

		if (!discrete && props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
		{
			discrete = physical_devices[i];
		}

		if (!fallback)
		{
			fallback = physical_devices[i];
		}
	}

	VkPhysicalDevice result = discrete ? discrete : fallback;
	if (result)
	{
		VkPhysicalDeviceProperties props;
		vkGetPhysicalDeviceProperties(result, &props);
		printf("Selected GPU: %s\n", props.deviceName);
	}
	else
	{
		printf("ERROR: No suitable GPUs found.\n");
	}

	return result;
}

VkDevice CreateDevice(VkInstance instance, VkPhysicalDevice physical_device, uint32_t family_index)
{
	assert(instance);
	assert(physical_device);

	const float queue_priorities[] = { 1.0f };

	VkDeviceQueueCreateInfo queue_create_info = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
	queue_create_info.queueFamilyIndex = family_index;
	queue_create_info.queueCount = 1;
	queue_create_info.pQueuePriorities = queue_priorities;

	// TODO?
	char const* const extensions[] = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
		VK_KHR_16BIT_STORAGE_EXTENSION_NAME,		// Using 16 bit in storage buffers
		VK_KHR_8BIT_STORAGE_EXTENSION_NAME,			// Using 8 bit in storage buffers
		VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME,	// Using 8/16 bit arithmetic in shaders
	};

	VkDeviceCreateInfo device_create_info = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	device_create_info.queueCreateInfoCount = 1;
	device_create_info.pQueueCreateInfos = &queue_create_info;
	device_create_info.ppEnabledExtensionNames = extensions;
	device_create_info.enabledExtensionCount = ARRAYSIZE(extensions);


	VkPhysicalDeviceFeatures2 features2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
	// VkPhysicalDeviceFeatures features = {};
	// features2.features.vertexPipelineStoresAndAtomics = VK_TRUE;	// TODO, for us it work, not for arseny.

	VkPhysicalDevice8BitStorageFeatures features_8bit = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES };
	features_8bit.storageBuffer8BitAccess = VK_TRUE;
	features_8bit.uniformAndStorageBuffer8BitAccess = VK_TRUE;	// TODO: the above alone doesn't work, but this does.
	// features_8bit.storagePushConstant8 = VK_TRUE;
	VkPhysicalDevice16BitStorageFeatures features_16bit = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES };
	features_16bit.storageBuffer16BitAccess = VK_TRUE;
	// features_16bit.uniformAndStorageBuffer16BitAccess;
	// features_16bit.storagePushConstant16;
	// features_16bit.storageInputOutput16;

	VkPhysicalDeviceShaderFloat16Int8FeaturesKHR features_f16i8 = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES_KHR
	};
	// TODO This makes everything crash, probably we should check if available?
	// features_f16i8.shaderFloat16 = VK_TRUE;
	features_f16i8.shaderInt8 = VK_TRUE;

	// device_create_info.pEnabledFeatures = &features;
	device_create_info.pNext = &features2;
	features2.pNext = &features_8bit;
	features_8bit.pNext = &features_16bit;
	features_16bit.pNext = &features_f16i8;	 // TODO

	VkDevice device = VK_NULL_HANDLE;
	VK_CHECK(vkCreateDevice(physical_device, &device_create_info, nullptr, &device));

	return device;
}

VkSurfaceKHR CreateSurface(VkInstance instance, GLFWwindow* window)
{
	assert(instance);
	assert(window);

#ifdef VK_USE_PLATFORM_WIN32_KHR
	VkWin32SurfaceCreateInfoKHR surface_create_info = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
	surface_create_info.hinstance = GetModuleHandle(nullptr);
	surface_create_info.hwnd = glfwGetWin32Window(window);
	VkSurfaceKHR surface = 0;
	VK_CHECK(vkCreateWin32SurfaceKHR(instance, &surface_create_info, nullptr, &surface));
	return surface;
#else
#error Unsupported Platform
#endif
}

VkFormat GetSwapchainFormat(VkPhysicalDevice physical_device, VkSurfaceKHR surface)
{
	uint32_t format_count = 0;
	VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, nullptr));
	assert(format_count > 0);

	std::vector<VkSurfaceFormatKHR> formats(format_count);
	VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, formats.data()));

	// If this condition is satisfied, we are allowed to pick any format we like.
	if (format_count == 1 && formats[0].format == VK_FORMAT_UNDEFINED)
	{
		return VK_FORMAT_R8G8B8A8_UNORM;
	}

	// Prefer high quality 32 bit UNORM format.
	// TODO: Prefer one of these for the VK_FORMAT_UNDEFINED case.
	for (uint32_t i = 0; i < format_count; ++i)
	{
		if (formats[i].format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 ||
				formats[i].format == VK_FORMAT_A2B10G10R10_UNORM_PACK32)
		{
			return formats[i].format;
		}
	}

	// Prefer 32 bit UNORM format.
	for (uint32_t i = 0; i < format_count; ++i)
	{
		if (formats[i].format == VK_FORMAT_R8G8B8A8_UNORM || formats[i].format == VK_FORMAT_B8G8R8A8_UNORM)
		{
			return formats[i].format;
		}
	}

	// TODO: deal with format_count == 0 or formats[0].format = VK_FORMAT_UNDEFINED.
	return formats[0].format;
}

VkSemaphore CreateSemaphore(VkDevice device)
{
	assert(device);
	VkSemaphoreCreateInfo semaphore_create_info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

	VkSemaphore semaphore = VK_NULL_HANDLE;
	VK_CHECK(vkCreateSemaphore(device, &semaphore_create_info, nullptr, &semaphore));

	return semaphore;
}

VkRenderPass CreateRenderPass(VkDevice device, VkFormat format)
{
	assert(device);
	assert(format);

	VkAttachmentDescription attachments[1] = {};
	attachments[0].format = format;
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference attachment_ref;
	attachment_ref.attachment = 0;	// Index into attachments for subpass.
	attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &attachment_ref;

	VkRenderPassCreateInfo pass_create_info = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
	pass_create_info.attachmentCount = ARRAYSIZE(attachments);
	pass_create_info.pAttachments = attachments;
	pass_create_info.subpassCount = 1;
	pass_create_info.pSubpasses = &subpass;

	VkRenderPass render_pass = VK_NULL_HANDLE;
	VK_CHECK(vkCreateRenderPass(device, &pass_create_info, nullptr, &render_pass));
	return render_pass;
}

VkFramebuffer
CreateFrameBuffer(VkDevice device, VkRenderPass render_pass, VkImageView image_view, uint32_t width, uint32_t height)
{
	assert(device);
	assert(render_pass);

	VkFramebufferCreateInfo fb_create_info = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
	fb_create_info.renderPass = render_pass;
	fb_create_info.attachmentCount = 1;
	fb_create_info.pAttachments = &image_view;
	fb_create_info.width = width;
	fb_create_info.height = height;
	fb_create_info.layers = 1;

	VkFramebuffer framebuffer = VK_NULL_HANDLE;
	VK_CHECK(vkCreateFramebuffer(device, &fb_create_info, nullptr, &framebuffer));
	return framebuffer;
};

VkImageView CreateImageView(VkDevice device, VkImage image, VkFormat format)
{
	assert(device);
	assert(image);
	assert(format);

	VkImageViewCreateInfo img_view_create_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	img_view_create_info.image = image;
	img_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	img_view_create_info.format = format;
	img_view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	img_view_create_info.subresourceRange.levelCount = 1;
	img_view_create_info.subresourceRange.layerCount = 1;

	VkImageView img_view = VK_NULL_HANDLE;
	VK_CHECK(vkCreateImageView(device, &img_view_create_info, nullptr, &img_view));
	return img_view;
}

VkShaderModule LoadShader(VkDevice device, const char* path)
{
	assert(device);

	FILE* file = fopen(path, "rb");
	assert(file);
	fseek(file, 0, SEEK_END);
	const long length = ftell(file);
	assert(length >= 0);
	assert(length % 4 == 0);
	fseek(file, 0, SEEK_SET);

	char* buffer = (char*)malloc(length);
	assert(buffer);

	const size_t rc = fread(buffer, 1, length, file);
	assert(rc == length);
	fclose(file);

	VkShaderModuleCreateInfo shader_create_info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	shader_create_info.codeSize = length;
	shader_create_info.pCode = (const uint32_t*)buffer;

	VkShaderModule shader_module = VK_NULL_HANDLE;
	VK_CHECK(vkCreateShaderModule(device, &shader_create_info, nullptr, &shader_module));

	free(buffer);
	return shader_module;
}

VkDescriptorSetLayout CreateDescriptorSetLayout(VkDevice device)
{
	VkDescriptorSetLayoutBinding set_layout_bindings[1] = {};
	set_layout_bindings[0].binding = 0;
	set_layout_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	set_layout_bindings[0].descriptorCount = 1;
	set_layout_bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	// set_layout_bindings[0].pImmutableSamplers;

	VkDescriptorSetLayoutCreateInfo set_layout_create_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	// I guess normally we'd go with VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT.
	// But since we're using the push extensions, it's like this:
	set_layout_create_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
	set_layout_create_info.bindingCount = ARRAYSIZE(set_layout_bindings);
	set_layout_create_info.pBindings = set_layout_bindings;

	VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
	VK_CHECK(vkCreateDescriptorSetLayout(device, &set_layout_create_info, nullptr, &set_layout));

	return set_layout;
}

VkPipelineLayout CreatePipelineLayout(VkDevice device, VkDescriptorSetLayout set_layout)
{
	VkPipelineLayoutCreateInfo layout_create_info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	layout_create_info.setLayoutCount = 1;
	layout_create_info.pSetLayouts = &set_layout;
	// layout_create_info.pushConstantRangeCount;
	// layout_create_info.pPushConstantRanges;

	VkPipelineLayout layout = VK_NULL_HANDLE;
	VK_CHECK(vkCreatePipelineLayout(device, &layout_create_info, nullptr, &layout));

	return layout;
}

VkPipeline CreateGraphicsPipeline(VkDevice device, VkPipelineCache pipeline_cache, VkRenderPass render_pass,
		VkPipelineLayout layout, VkShaderModule vert, VkShaderModule frag)
{
	assert(device);
	assert(vert);
	assert(frag);

	VkPipelineShaderStageCreateInfo stages[2] = {};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vert;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = frag;
	stages[1].pName = "main";

	VkPipelineVertexInputStateCreateInfo vertex_input = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

	const bool kUseVertexAttributeDescs = false;
	if (kUseVertexAttributeDescs)
	{
		// TODO: tmp
		VkVertexInputBindingDescription stream = {};
		stream.binding = 0;
		stream.stride = sizeof(Vertex);
		stream.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		VkVertexInputAttributeDescription attrs[3] = {};
		attrs[0].location = 0;
		attrs[0].binding = 0;
		attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		attrs[0].offset = 0;
		attrs[1].location = 1;
		attrs[1].binding = 0;
		attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
		attrs[1].offset = 12;
		attrs[2].location = 2;
		attrs[2].binding = 0;
		attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
		attrs[2].offset = 24;

		vertex_input.vertexBindingDescriptionCount = 1;
		vertex_input.pVertexBindingDescriptions = &stream;
		vertex_input.vertexAttributeDescriptionCount = ARRAYSIZE(attrs);
		vertex_input.pVertexAttributeDescriptions = attrs;
	}


	VkPipelineInputAssemblyStateCreateInfo input_assembly = {
		VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO
	};
	input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineViewportStateCreateInfo viewport = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
	// Don't bake anything into the pipeline
	viewport.viewportCount = 1;
	// viewport.pViewports;
	viewport.scissorCount = 1;
	// viewport.pScissors;

	VkPipelineRasterizationStateCreateInfo raster_state = {
		VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO
	};
	raster_state.polygonMode = VK_POLYGON_MODE_FILL;
	// TODO: Count on 0 being ok for all this.
	// raster_state.cullMode;
	// raster_state.frontFace;
	// raster_state.depthBiasEnable;
	// raster_state.depthBiasConstantFactor;
	// raster_state.depthBiasClamp;
	// raster_state.depthBiasSlopeFactor;
	raster_state.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisample = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	// multisample.sampleShadingEnable;
	// multisample.minSampleShading;
	// multisample.pSampleMask;
	// multisample.alphaToCoverageEnable;
	// multisample.alphaToOneEnable;

	VkPipelineDepthStencilStateCreateInfo depth_stencil = {
		VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO
	};
	// depth_stencil.depthTestEnable;
	// depth_stencil.depthWriteEnable;
	// depth_stencil.depthCompareOp;
	// depth_stencil.depthBoundsTestEnable;
	// depth_stencil.stencilTestEnable;
	// depth_stencil.front;
	// depth_stencil.back;
	// depth_stencil.minDepthBounds;
	// depth_stencil.maxDepthBounds;

	VkPipelineColorBlendAttachmentState attachments[1] = {};
	attachments[0].colorWriteMask =
			VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo blend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
	// blend.logicOpEnable;
	// blend.logicOp;
	blend.attachmentCount = ARRAYSIZE(attachments);
	blend.pAttachments = attachments;
	// blend.blendConstants[4];

	VkDynamicState dynamic_states[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
	};

	VkPipelineDynamicStateCreateInfo dynamic = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	dynamic.dynamicStateCount = ARRAYSIZE(dynamic_states);
	dynamic.pDynamicStates = dynamic_states;

	VkGraphicsPipelineCreateInfo pipeline_create_info = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	pipeline_create_info.stageCount = ARRAYSIZE(stages);
	pipeline_create_info.pStages = stages;
	pipeline_create_info.pVertexInputState = &vertex_input;
	pipeline_create_info.pInputAssemblyState = &input_assembly;

	pipeline_create_info.pViewportState = &viewport;
	pipeline_create_info.pRasterizationState = &raster_state;
	pipeline_create_info.pMultisampleState = &multisample;
	pipeline_create_info.pDepthStencilState = &depth_stencil;
	pipeline_create_info.pColorBlendState = &blend;
	pipeline_create_info.pDynamicState = &dynamic;
	pipeline_create_info.layout = layout;
	pipeline_create_info.renderPass = render_pass;
	// pipeline_create_info.subpass = 0;

	VkPipeline pipeline = VK_NULL_HANDLE;
	VK_CHECK(vkCreateGraphicsPipelines(device, pipeline_cache, 1, &pipeline_create_info, nullptr, &pipeline));
	return pipeline;
}

VkCommandPool CreateCommandBufferPool(VkDevice device, uint32_t family_index)
{
	assert(device);
	VkCommandPoolCreateInfo cmd_pool_create_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	cmd_pool_create_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	cmd_pool_create_info.queueFamilyIndex = family_index;

	VkCommandPool cmd_pool = VK_NULL_HANDLE;
	vkCreateCommandPool(device, &cmd_pool_create_info, nullptr, &cmd_pool);

	return cmd_pool;
}

VkImageMemoryBarrier ImageBarrier(VkImage image, VkAccessFlags src_access_mask, VkAccessFlags dst_access_mask,
		VkImageLayout old_layout, VkImageLayout new_layout)
{
	VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	barrier.srcAccessMask = src_access_mask;
	barrier.dstAccessMask = dst_access_mask;
	barrier.oldLayout = old_layout;
	barrier.newLayout = new_layout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	// barrier.subresourceRange.baseMipLevel;
	barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;	  // NOTE: Some Android driver ignore this.
																	  // barrier.subresourceRange.baseArrayLayer;
	barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;  // NOTE: Some Android driver ignore this.

	return barrier;
}

VkSwapchainKHR CreateSwapchain(VkDevice device, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR surface_caps,
		VkFormat format, uint32_t family_index, uint32_t width, uint32_t height, VkSwapchainKHR old_swapchain)
{
	assert(device);
	assert(surface);
	assert(format);

	const VkCompositeAlphaFlagBitsKHR surface_composite =
			(surface_caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) ?
			  VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR :
			(surface_caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) ?
			  VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR :
			(surface_caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR) ?
			  VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR :
			  VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;	 // One option is always guaranteed to be supported.

	VkSwapchainCreateInfoKHR swapchain_create_info = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
	swapchain_create_info.surface = surface;
	swapchain_create_info.minImageCount = std::max(2u, surface_caps.minImageCount);
	swapchain_create_info.imageFormat = format;
	swapchain_create_info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	swapchain_create_info.imageExtent.width = width;
	swapchain_create_info.imageExtent.height = height;
	swapchain_create_info.imageArrayLayers = 1;
	swapchain_create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	swapchain_create_info.queueFamilyIndexCount = 1;
	swapchain_create_info.pQueueFamilyIndices = &family_index;
	// TODO: There was a validation error about the preTransform.
	// Calling vkGetPhysicalDeviceSurfaceCapabilitiesKHR made it go away. Strange.
	// I feel it should be:
	swapchain_create_info.preTransform = surface_caps.currentTransform;
	// swapchain_create_info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	// NOTE: Android doesn't support opaque bit, it supports 0x2 or 0x4.
	swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;  // surface_composite;
	swapchain_create_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
	swapchain_create_info.oldSwapchain = old_swapchain;

	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	VK_CHECK(vkCreateSwapchainKHR(device, &swapchain_create_info, nullptr, &swapchain));

	return swapchain;
}

void CreateSwapchain(VkPhysicalDevice physical_device, VkDevice device, VkSurfaceKHR surface, VkFormat format,
		uint32_t family_index, VkRenderPass render_pass, VkSwapchainKHR old_swapchain, Swapchain& result)
{
	// TODO: asserts?

	VkSurfaceCapabilitiesKHR surface_caps;
	VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &surface_caps));
	const uint32_t width = surface_caps.currentExtent.width;
	const uint32_t height = surface_caps.currentExtent.height;

	// TODO: merge the overload in here?
	VkSwapchainKHR swapchain =
			CreateSwapchain(device, surface, surface_caps, format, family_index, width, height, old_swapchain);
	assert(swapchain);

	uint32_t image_count = 0;
	VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &image_count, nullptr));

	std::vector<VkImage> images(image_count);
	VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &image_count, images.data()));

	std::vector<VkImageView> image_views(image_count);
	for (uint32_t i = 0; i < image_count; ++i)
	{
		image_views[i] = CreateImageView(device, images[i], format);
		assert(image_views[i]);
	}

	std::vector<VkFramebuffer> framebuffers(image_count);
	for (uint32_t i = 0; i < image_count; ++i)
	{
		framebuffers[i] = CreateFrameBuffer(device, render_pass, image_views[i], width, height);
		assert(framebuffers[i]);
	}

	// TODO: unnecessary copying.
	result.swapchain = swapchain;
	result.images = images;
	result.image_views = image_views;
	result.framebuffers = framebuffers;
	result.width = width;
	result.height = height;
	result.image_count = image_count;

	// TODO: error handling?
}

void ResizeSwapchainIfNecessary(VkPhysicalDevice physical_device, VkDevice device, VkSurfaceKHR surface,
		VkFormat format, uint32_t family_index, VkRenderPass render_pass, Swapchain& result)
{
	// TODO: asserts
	assert(result.swapchain);

	// TODO: Handle minimization (crashes on NVidia)
	// vulkan-tutorial does:
	// int width = 0, height = 0;
	// glfwGetFramebufferSize(window, &width, &height);
	// while (width == 0 || height == 0)
	// {
	// 	glfwGetFramebufferSize(window, &width, &height);
	// 	glfwWaitEvents();
	// }
	// TODO: https://vulkan-tutorial.com/Drawing_a_triangle/Swap_chain_recreation#page_Handling-resizes-explicitly
	// vulkan-tutorial relies on the output of vkQueuePresentKHR

	VkSurfaceCapabilitiesKHR surface_caps;
	VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &surface_caps));
	const uint32_t curr_width = surface_caps.currentExtent.width;
	const uint32_t curr_height = surface_caps.currentExtent.height;

	if (curr_width != result.width || curr_height != result.height)
	{
		// TODO: So much  copying.
		Swapchain old = result;

		// TODO: This will query the caps again.
		CreateSwapchain(physical_device, device, surface, format, family_index, render_pass, old.swapchain, result);

		VK_CHECK(vkDeviceWaitIdle(device));

		DestroySwapchain(device, old);
	}
}

void DestroySwapchain(VkDevice device, const Swapchain& swapchain)
{
	// TODO: asserts

	for (uint32_t i = 0; i < swapchain.image_count; ++i)
	{
		vkDestroyFramebuffer(device, swapchain.framebuffers[i], nullptr);
	}
	for (uint32_t i = 0; i < swapchain.image_count; ++i)
	{
		vkDestroyImageView(device, swapchain.image_views[i], nullptr);
	}
	vkDestroySwapchainKHR(device, swapchain.swapchain, nullptr);
}
