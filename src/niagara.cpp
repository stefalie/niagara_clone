#include <assert.h>
#include <stdio.h>
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <vulkan/vulkan.h>

// SHORTCUT: Would need to be checked properly in production.
#define VK_CHECK(call) \
	do \
	{ \
		VkResult rc = call; \
		assert(rc == VK_SUCCESS); \
	} while(0)

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

VkInstance CreateInstance();
uint32_t GetGraphicsQueueFamily(VkPhysicalDevice physical_device);
// SHORTUCT: We just go for the first discrete GPU, or, if not available, simply the first GPU.
VkPhysicalDevice PickPhysicalDevice(VkInstance instance);
VkDevice CreateDevice(VkInstance instance, VkPhysicalDevice physical_device, uint32_t family_index);
VkSurfaceKHR CreateSurface(VkInstance instance, GLFWwindow* window);
VkFormat GetSwapchainFormat(VkPhysicalDevice physical_device, VkSurfaceKHR surface);
VkSwapchainKHR CreateSwapchain(VkDevice device, VkSurfaceKHR surface, VkFormat format, uint32_t family_index, uint32_t width, uint32_t height);
VkSemaphore CreateSemaphore(VkDevice device);
VkRenderPass CreateRenderPass(VkDevice device, VkFormat format);
VkFramebuffer CreateFrameBuffer(VkDevice device, VkRenderPass render_pass, VkImageView image_view, uint32_t width, uint32_t height);
VkImageView CreateImageView(VkDevice device, VkImage image, VkFormat format);
VkShaderModule LoadShader(VkDevice device, const char* path);
VkPipelineLayout CreatePipelineLayout(VkDevice device);
VkPipeline CreateGraphicsPipeline(VkDevice device, VkPipelineCache pipeline_cache, VkRenderPass render_pass, VkPipelineLayout layout, VkShaderModule vert, VkShaderModule frag);
VkCommandPool CreateCommandBufferPool(VkDevice device, uint32_t family_index);

int main()
{
	const int rc = glfwInit();
	assert(rc == 1);

	VkInstance instance = CreateInstance();
	assert(instance);

	VkPhysicalDevice physical_device = PickPhysicalDevice(instance);
	assert(physical_device);

	const uint32_t family_index = GetGraphicsQueueFamily(physical_device);
	assert(family_index != ~0u);

	VkDevice device = CreateDevice(instance, physical_device, family_index);
	assert(device);

	const int window_width = 1024;
	const int window_height = 768;
	GLFWwindow* window = glfwCreateWindow(window_width, window_height, "Hello Vulkan", nullptr, nullptr);
	assert(window);

	VkSurfaceKHR surface = CreateSurface(instance, window);
	assert(surface);

	// TODO: It's stupid that you need to first open a windows and create a surface just to see if its suuported.
	VkBool32 is_surface_supported = false;
	VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, family_index, surface, &is_surface_supported));
	assert(is_surface_supported);

	int fb_width, fb_height;
	glfwGetFramebufferSize(window, &fb_width, &fb_height);

	VkFormat swapchain_format = GetSwapchainFormat(physical_device, surface);
	assert(swapchain_format);

	VkSwapchainKHR swapchain = CreateSwapchain(device, surface, swapchain_format, family_index, fb_width, fb_height);
	assert(swapchain);

	VkSemaphore aquire_semaphore = CreateSemaphore(device);
	assert(aquire_semaphore);
	VkSemaphore release_semaphore = CreateSemaphore(device);
	assert(release_semaphore);

	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(device, family_index, 0, &queue);
	assert(queue);

	VkRenderPass render_pass = CreateRenderPass(device, swapchain_format);
	assert(render_pass);

	VkShaderModule triangle_vert = LoadShader(device, "shaders/triangle.vert.spv");
	VkShaderModule triangle_frag = LoadShader(device, "shaders/triangle.frag.spv");

	// TODO: Critical for perf.
	VkPipelineCache pipeline_cache = VK_NULL_HANDLE;

	VkPipelineLayout triangle_layout = CreatePipelineLayout(device);

	VkPipeline triangle_pipeline = CreateGraphicsPipeline(device, pipeline_cache, render_pass, triangle_layout, triangle_vert, triangle_frag);
	assert(triangle_pipeline);

	// TODO: get rid of this.
	VkImage swapchain_images[16];  // SHORTCUT!
	uint32_t swapchain_image_count = ARRAY_SIZE(swapchain_images);
	VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &swapchain_image_count, nullptr));  // TODO: when there are more than 16 -> crash
	swapchain_image_count = min(swapchain_image_count, 16);
	VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &swapchain_image_count, swapchain_images));

	VkImageView swapchain_image_views[16];
	for (uint32_t i = 0; i < swapchain_image_count; ++i)
	{
		swapchain_image_views[i] = CreateImageView(device, swapchain_images[i], swapchain_format);
	}

	VkFramebuffer swapchain_framebuffers[16];
	for (uint32_t i = 0; i < swapchain_image_count; ++i)
	{
		swapchain_framebuffers[i] = CreateFrameBuffer(device, render_pass, swapchain_image_views[i], fb_width, fb_height);
		assert(swapchain_framebuffers[i]);
	}

	VkCommandPool cmd_buf_pool = CreateCommandBufferPool(device, family_index);
	assert(cmd_buf_pool);

	VkCommandBufferAllocateInfo cmd_buf_alloc_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	cmd_buf_alloc_info.commandPool = cmd_buf_pool;
	cmd_buf_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmd_buf_alloc_info.commandBufferCount = 1;

	VkCommandBuffer cmd_buf = VK_NULL_HANDLE;
	VK_CHECK(vkAllocateCommandBuffers(device, &cmd_buf_alloc_info, &cmd_buf));

	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();

		uint32_t image_index = 0;
		VK_CHECK(vkAcquireNextImageKHR(device, swapchain, ~0ull, aquire_semaphore, VK_NULL_HANDLE, &image_index));

		VK_CHECK(vkResetCommandPool(device, cmd_buf_pool, 0));

		VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		VK_CHECK(vkBeginCommandBuffer(cmd_buf, &begin_info));

		VkClearColorValue clear_color = { 48.0f / 255.0f, 10.0f / 255.0f, 36.0f / 255.0f, 1.0f };  // Ubuntu terminal color.
		VkClearValue clear_value = { clear_color };

		// TODO
		//vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_DEP)

		VkRenderPassBeginInfo pass_begin_info = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
		pass_begin_info.renderPass = render_pass;
		pass_begin_info.framebuffer = swapchain_framebuffers[image_index];
		pass_begin_info.renderArea.extent.width = fb_width;
		pass_begin_info.renderArea.extent.height = fb_height;
		pass_begin_info.clearValueCount = 1;
		pass_begin_info.pClearValues = &clear_value;

		vkCmdBeginRenderPass(cmd_buf, &pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport viewport = { 0.0f, (float)fb_height, (float)fb_width, -(float)fb_height, 0.0f, 1.0f };
		VkRect2D scissor = { { 0, 0 }, { (uint32_t)fb_width, (uint32_t)fb_height } };

		vkCmdSetViewport(cmd_buf, 0, 1, &viewport);
		vkCmdSetScissor(cmd_buf, 0, 1, &scissor);

		vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, triangle_pipeline);
		vkCmdDraw(cmd_buf, 3, 1, 0, 0);

		vkCmdEndRenderPass(cmd_buf);

		//VkClearColorValue color = { 1, 0, 1, 1 };
		//VkImageSubresourceRange range = {};
		//range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		//range.levelCount = 1;
		//range.layerCount = 1;
		//vkCmdClearColorImage(cmd_buf, swapchain_images[image_index], VK_IMAGE_LAYOUT_GENERAL, &color, 1, &range);

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
		present_info.pSwapchains = &swapchain;
		present_info.pImageIndices = &image_index;

		VK_CHECK(vkQueuePresentKHR(queue, &present_info));

		VK_CHECK(vkDeviceWaitIdle(device));
	}

	// SHORTCUT: Destroy surface, instance, etc.
	vkDestroyCommandPool(device, cmd_buf_pool, nullptr);


	glfwDestroyWindow(window);

	return 0;
}

VkInstance CreateInstance()
{
	// SHORTCUT: Check if version is available via vkEnumerateInstanceVersion()
	VkApplicationInfo app_info = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
	app_info.apiVersion = VK_API_VERSION_1_2;
	app_info.pApplicationName = "Niagara Clone Test";

	VkInstanceCreateInfo instance_create_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	instance_create_info.pApplicationInfo = &app_info;

	// SHORTCUT: Check availability of theses?
	char const* const debug_layers[] =
	{
		"VK_LAYER_KHRONOS_validation",
		// TODO: What's the difference? I believe above supersedes this.
		//"VK_LAYER_LUNARG_standard_validation",
		// TODO: Figure out how to use that.
		// "VK_LAYER_RENDERDOC_Capture",

	};
#ifdef _DEBUG
	instance_create_info.ppEnabledLayerNames = debug_layers;
	instance_create_info.enabledLayerCount = ARRAY_SIZE(debug_layers);
#endif

	char const* const extensions[] =
	{
		VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef VK_USE_PLATFORM_WIN32_KHR
		VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif
	};
	instance_create_info.ppEnabledExtensionNames = extensions;
	instance_create_info.enabledExtensionCount = ARRAY_SIZE(extensions);

	VkInstance instance = VK_NULL_HANDLE;
	VK_CHECK(vkCreateInstance(&instance_create_info, nullptr, &instance));

	return instance;
}

uint32_t GetGraphicsQueueFamily(VkPhysicalDevice physical_device)
{
	VkQueueFamilyProperties queue_family_properties[64];
	uint32_t queue_family_count = ARRAY_SIZE(queue_family_properties);
	vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_family_properties);

	for (uint32_t i = 0; i < queue_family_count; ++i)
	{
		if (queue_family_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
		{
			return i;
		}
	}

	// TODO: Use in PickPhysicalDevice to pick rasterization-capable device.
	assert(!"No graphics queue family available! Your device doesn't support rasterization?");
	return ~0u;
}

VkPhysicalDevice PickPhysicalDevice(VkInstance instance)
{
	assert(instance);

	// SHORTCUT: Call twice to first retrieve the number of physical devices.
	// But hey, who will ever have more than 16 GPUs?
	VkPhysicalDevice physical_devices[16];
	uint32_t physical_device_count = ARRAY_SIZE(physical_devices);
	vkEnumeratePhysicalDevices(instance, &physical_device_count, physical_devices);

	VkPhysicalDevice ret = VK_NULL_HANDLE;

	// Pick 1st discrete GPU.
	for (uint32_t i = 0; i < physical_device_count; ++i)
	{
		VkPhysicalDeviceProperties props;
		vkGetPhysicalDeviceProperties(physical_devices[i], &props);

		if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
		{
			printf("Picking first available discrete GPU: %s.\n", props.deviceName);
			ret = physical_devices[i];
			break;
		}
	}

	// Pick 1st GPU.
	if ((ret == VK_NULL_HANDLE) && (physical_device_count > 0))
	{
		VkPhysicalDeviceProperties props;
		vkGetPhysicalDeviceProperties(physical_devices[0], &props);

		printf("Picking first available GPU: %s.\n", props.deviceName);
		ret = physical_devices[0];
	}

	if (ret == VK_NULL_HANDLE)
	{
		printf("No physical devices available.\n");
	}

	return ret;
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

	char const* const extensions[] =
	{
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
	};

	VkDeviceCreateInfo device_create_info = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	device_create_info.queueCreateInfoCount = 1;
	device_create_info.pQueueCreateInfos = &queue_create_info;
	device_create_info.ppEnabledExtensionNames = extensions;
	device_create_info.enabledExtensionCount = ARRAY_SIZE(extensions);

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
	VkSurfaceFormatKHR formats[16];
	uint32_t format_count = ARRAY_SIZE(formats);
	// TODO: Deal with VK_INCOMPLETE when there are more than 16 formats.
	VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, formats));

	// TODO: deal with format_count == 0 or formats[0].format = VK_FORMAT_UNDEFINED.
	return formats[0].format;
}

VkSwapchainKHR CreateSwapchain(VkDevice device, VkSurfaceKHR surface, VkFormat format, uint32_t family_index, uint32_t width, uint32_t height)
{
	assert(device);
	assert(surface);
	assert(format);

	VkSwapchainCreateInfoKHR swapchain_create_info = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
	swapchain_create_info.surface = surface;
	swapchain_create_info.minImageCount = 2;
	swapchain_create_info.imageFormat = format;
	swapchain_create_info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	swapchain_create_info.imageExtent.width = width;
	swapchain_create_info.imageExtent.height = height;
	swapchain_create_info.imageArrayLayers = 1;
	swapchain_create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	swapchain_create_info.queueFamilyIndexCount = 1;
	swapchain_create_info.pQueueFamilyIndices = &family_index;
	swapchain_create_info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapchain_create_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;

	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	VK_CHECK(vkCreateSwapchainKHR(device, &swapchain_create_info, nullptr, &swapchain));

	return swapchain;
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
    attachment_ref.attachment = 0;  // Index into attachments for subpass.
    attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &attachment_ref;

	VkRenderPassCreateInfo pass_create_info = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    pass_create_info.attachmentCount = ARRAY_SIZE(attachments);
    pass_create_info.pAttachments = attachments;
    pass_create_info.subpassCount = 1;
	pass_create_info.pSubpasses = &subpass;

	VkRenderPass render_pass = VK_NULL_HANDLE;
	VK_CHECK(vkCreateRenderPass(device, &pass_create_info, nullptr, &render_pass));
	return render_pass;
}

VkFramebuffer CreateFrameBuffer(VkDevice device, VkRenderPass render_pass, VkImageView image_view, uint32_t width, uint32_t height)
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

VkPipelineLayout CreatePipelineLayout(VkDevice device)
{
	VkPipelineLayoutCreateInfo layout_create_info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	//layout_create_info.setLayoutCount;
	//layout_create_info.pSetLayouts;
	//layout_create_info.pushConstantRangeCount;
	//layout_create_info.pPushConstantRanges;

	VkPipelineLayout layout = VK_NULL_HANDLE;
	VK_CHECK(vkCreatePipelineLayout(device, &layout_create_info, nullptr, &layout));
	return layout;
}

VkPipeline CreateGraphicsPipeline(VkDevice device, VkPipelineCache pipeline_cache, VkRenderPass render_pass, VkPipelineLayout layout, VkShaderModule vert, VkShaderModule frag)
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
	//jvertex_input.vertexBindingDescriptionCount;
	//jvertex_input.pVertexBindingDescriptions;
	//jvertex_input.vertexAttributeDescriptionCount;
	//jvertex_input.pVertexAttributeDescriptions;

	VkPipelineInputAssemblyStateCreateInfo input_assembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
	input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineViewportStateCreateInfo viewport = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
	// Don't bake anything into the pipeline
	viewport.viewportCount = 1;
	//viewport.pViewports;
	viewport.scissorCount = 1;
	//viewport.pScissors;

	VkPipelineRasterizationStateCreateInfo raster_state = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
	raster_state.polygonMode = VK_POLYGON_MODE_FILL;
	// TODO: Count on 0 being ok for all this.
	//raster_state.cullMode;
	//raster_state.frontFace;
	//raster_state.depthBiasEnable;
	//raster_state.depthBiasConstantFactor;
	//raster_state.depthBiasClamp;
	//raster_state.depthBiasSlopeFactor;
	raster_state.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisample = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	//multisample.sampleShadingEnable;
	//multisample.minSampleShading;
	//multisample.pSampleMask;
	//multisample.alphaToCoverageEnable;
	//multisample.alphaToOneEnable;

	VkPipelineDepthStencilStateCreateInfo depth_stencil = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
	//depth_stencil.depthTestEnable;
	//depth_stencil.depthWriteEnable;
	//depth_stencil.depthCompareOp;
	//depth_stencil.depthBoundsTestEnable;
	//depth_stencil.stencilTestEnable;
	//depth_stencil.front;
	//depth_stencil.back;
	//depth_stencil.minDepthBounds;
	//depth_stencil.maxDepthBounds;

	VkPipelineColorBlendAttachmentState attachments[1] = {};
	attachments[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo blend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    //blend.logicOpEnable;
    //blend.logicOp;
	blend.attachmentCount = ARRAY_SIZE(attachments);
    blend.pAttachments = attachments;
    //blend.blendConstants[4];

	VkDynamicState dynamic_states[] =
	{
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
	};

	VkPipelineDynamicStateCreateInfo dynamic = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	dynamic.dynamicStateCount = ARRAY_SIZE(dynamic_states);
	dynamic.pDynamicStates = dynamic_states;

	VkGraphicsPipelineCreateInfo pipeline_create_info = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pipeline_create_info.stageCount = ARRAY_SIZE(stages);
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
    //pipeline_create_info.subpass = 0;

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
