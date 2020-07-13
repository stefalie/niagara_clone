#include <assert.h>
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#define VK_CHECK(call) \
	do { \
		VkResult rc = call; \
		assert(rc == VK_SUCCESS); \
	} while(0)

int main()
{
	int rc = glfwInit();
	assert(rc == 1);

	// SHORTCUT: Check if version is available via vkEnumerateInstanceVersion()
	VkApplicationInfo app_info = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
	app_info.apiVersion = VK_VERSION_1_2;

	VkInstance instance = 0;
	VkInstanceCreateInfo create_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	create_info.pApplicationInfo = &app_info;
	VK_CHECK(vkCreateInstance(&create_info, nullptr, &instance));

	GLFWwindow* window;
	window = glfwCreateWindow(1024, 768, "Hello Vulkan", nullptr, nullptr);
	assert(window);

	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();
	}

	glfwDestroyWindow(window);

	return 0;
}
