#include "common.h"

#include "swapchain.h"

#include <algorithm>

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#define VSYNC 0

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

// TODO: rename because of name clash below and/or inline?
static VkSwapchainKHR CreateSwapchain(VkDevice device, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR surface_caps,
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
            VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;  // One option is always guaranteed to be supported.

	VkSwapchainCreateInfoKHR swapchain_create_info = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
	swapchain_create_info.surface = surface;
	swapchain_create_info.minImageCount = std::max(2u, surface_caps.minImageCount);
	swapchain_create_info.imageFormat = format;
	swapchain_create_info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	swapchain_create_info.imageExtent.width = width;
	swapchain_create_info.imageExtent.height = height;
	swapchain_create_info.imageArrayLayers = 1;
	swapchain_create_info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	// TODO
	swapchain_create_info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT /*TODO*/;
	swapchain_create_info.queueFamilyIndexCount = 1;
	swapchain_create_info.pQueueFamilyIndices = &family_index;
	// TODO: There was a validation error about the preTransform.
	// Calling vkGetPhysicalDeviceSurfaceCapabilitiesKHR made it go away. Strange.
	// I feel it should be:
	swapchain_create_info.preTransform = surface_caps.currentTransform;
	// swapchain_create_info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	// NOTE: Android doesn't support opaque bit, it supports 0x2 or 0x4.
	swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;  // surface_composite;
	// According to Arseny, querying for V-Sync on NVidia will tell you that
	// it's not available even though it is, and if you enable it anyway, the
	// validation layers will be upset.
	swapchain_create_info.presentMode = VSYNC ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR;
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

	result.swapchain = swapchain;
	result.images = images;
	result.width = width;
	result.height = height;
	result.image_count = image_count;

	// TODO: error handling?
}

bool ResizeSwapchainIfNecessary(VkPhysicalDevice physical_device, VkDevice device, VkSurfaceKHR surface,
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

		return true;
	}
	else
	{
		return false;
	}
}

void DestroySwapchain(VkDevice device, const Swapchain& swapchain)
{
	vkDestroySwapchainKHR(device, swapchain.swapchain, nullptr);
}
