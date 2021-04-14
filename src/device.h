#pragma once

VkInstance CreateInstance();

VkDebugReportCallbackEXT RegisterDebugCallback(VkInstance instance);
VkDebugUtilsMessengerEXT RegisterDebugUtilsMessenger(VkInstance instance);

uint32_t GetGraphicsFamilyIndex(VkPhysicalDevice physical_device);

VkPhysicalDevice PickPhysicalDevice(VkInstance instance);

VkDevice CreateDevice(VkInstance instance, VkPhysicalDevice physical_device, uint32_t family_index, bool rtx_supported);
