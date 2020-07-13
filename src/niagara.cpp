#include <assert.h>
#include <stdio.h>
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

int main()
{
	printf("Hello Vulkan!\n");

	int rc = glfwInit();
	assert(rc == 1);

	GLFWwindow* window;
	window = glfwCreateWindow(1024, 768, "Hello Vulkan", nullptr, nullptr);
	assert(window);

	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();
	}

	glfwDestroyWindow(window);

	return 0;
}
