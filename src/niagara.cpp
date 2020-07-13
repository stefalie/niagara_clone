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

// SHORTUCT: We just go for the first discrete GPU, or, if not available, simply the first GPU.
VkPhysicalDevice PickPhysicalDevice(VkInstance instance);

VkDevice CreateDevice(VkInstance instance, VkPhysicalDevice physical_device, uint32_t* family_index);

VkSurfaceKHR CreateSurface(VkInstance instance, GLFWwindow* window);

VkSwapchainKHR CreateSwapchain(VkPhysicalDevice physical_device/*TODO*/, VkDevice device, VkSurfaceKHR surface, uint32_t family_index, uint32_t width, uint32_t height);

VkSemaphore CreateSemaphore(VkDevice device);

VkCommandPool CreateCommandBufferPool(VkDevice device, uint32_t family_index);

int main()
{
	const int rc = glfwInit();
	assert(rc == 1);

	VkInstance instance = CreateInstance();
	assert(instance);

	VkPhysicalDevice physical_device = PickPhysicalDevice(instance);
	assert(physical_device);

	uint32_t family_index = 0;
	VkDevice device = CreateDevice(instance, physical_device, &family_index);
	assert(device);

	GLFWwindow* window = glfwCreateWindow(1024, 768, "Hello Vulkan", nullptr, nullptr);
	assert(window);

	VkSurfaceKHR surface = CreateSurface(instance, window);
	assert(surface);

	int fb_width, fb_height;
	glfwGetFramebufferSize(window, &fb_width, &fb_height);
	VkSwapchainKHR swapchain = CreateSwapchain(physical_device, device, surface, family_index, fb_width, fb_height);
	assert(swapchain);

	VkSemaphore aquire_semaphore = CreateSemaphore(device);
	assert(aquire_semaphore);
	VkSemaphore release_semaphore = CreateSemaphore(device);
	assert(release_semaphore);

	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(device, family_index, 0, &queue);
	assert(queue);

	// TODO: get rid of this.
	VkImage swapchain_images[16];  // SHORTCUT!
	uint32_t swapchain_image_count = ARRAY_SIZE(swapchain_images);
	VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &swapchain_image_count, nullptr));
	VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &swapchain_image_count, swapchain_images));

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

		VkClearColorValue color = { 1, 0, 1, 1 };
		VkImageSubresourceRange range = {};
		range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		range.levelCount = 1;
		range.layerCount = 1;
		vkCmdClearColorImage(cmd_buf, swapchain_images[image_index], VK_IMAGE_LAYOUT_GENERAL, &color, 1, &range);

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
	app_info.apiVersion = VK_VERSION_1_2;
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

VkDevice CreateDevice(VkInstance instance, VkPhysicalDevice physical_device, uint32_t* family_index)
{
	assert(instance);
	assert(physical_device);

	// SHORTCUT: This needs to be computed from queue properties.
	*family_index = 0;

	// TODO
	//vkGetPhysicalDeviceQueueFamilyProperties();

	const float queue_priorities[] = { 1.0f };

	VkDeviceQueueCreateInfo queue_create_info = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
	queue_create_info.queueFamilyIndex = *family_index;
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

VkSwapchainKHR CreateSwapchain(VkPhysicalDevice physical_device/*TODO*/, VkDevice device, VkSurfaceKHR surface, uint32_t family_index, uint32_t width, uint32_t height)
{
	assert(device);
	assert(surface);

	VkSwapchainCreateInfoKHR swapchain_create_info = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
	swapchain_create_info.surface = surface;
	swapchain_create_info.minImageCount = 2;
	swapchain_create_info.imageFormat = VK_FORMAT_R8G8B8A8_UNORM;  // SHORTCUT: some devices only support BGRA
	swapchain_create_info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	swapchain_create_info.imageExtent.width = width;
	swapchain_create_info.imageExtent.height = height;
	swapchain_create_info.imageArrayLayers = 1;
	swapchain_create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	swapchain_create_info.queueFamilyIndexCount = 1;
	swapchain_create_info.pQueueFamilyIndices = &family_index;
	swapchain_create_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;

	// TODO
	swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapchain_create_info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	VkBool32 is_surface_supported = false;
	VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, family_index, surface, &is_surface_supported));
	assert(is_surface_supported);

	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	VK_CHECK(vkCreateSwapchainKHR(device, &swapchain_create_info, nullptr, &swapchain));

	return swapchain;
}

VkSemaphore CreateSemaphore(VkDevice device)
{
	assert(device);
	VkSemaphoreCreateInfo semaphore_create_info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

	VkSemaphore semaphore = VK_NULL_HANDLE;
	vkCreateSemaphore(device, &semaphore_create_info, nullptr, &semaphore);

	return semaphore;
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
