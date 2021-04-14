#pragma once

struct Swapchain
{
	VkSwapchainKHR swapchain;

	std::vector<VkImage> images;

	uint32_t width;
	uint32_t height;
	uint32_t image_count;  // TODO: Redundant
};

typedef struct GLFWwindow GLFWwindow;

VkSurfaceKHR CreateSurface(VkInstance instance, GLFWwindow* window);
VkFormat GetSwapchainFormat(VkPhysicalDevice physical_device, VkSurfaceKHR surface);

void CreateSwapchain(VkPhysicalDevice physical_device, VkDevice device, VkSurfaceKHR surface, VkFormat format,
		uint32_t family_index, VkRenderPass render_pass, VkSwapchainKHR old_swapchain, Swapchain& result);

bool ResizeSwapchainIfNecessary(VkPhysicalDevice physical_device, VkDevice device, VkSurfaceKHR surface,
		VkFormat format, uint32_t family_index, VkRenderPass render_pass, Swapchain& result);

void DestroySwapchain(VkDevice device, const Swapchain& swapchain);
