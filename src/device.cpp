#include "common.h"

#include "device.h"

#ifdef _WIN32
#include <Windows.h>
#endif

static bool CheckLayerSupport(const std::vector<const char*>& layers)
{
	uint32_t layer_count = 0;
	VK_CHECK(vkEnumerateInstanceLayerProperties(&layer_count, nullptr));
	std::vector<VkLayerProperties> available_layers(layer_count);
	VK_CHECK(vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data()));

	for (const char* requested_layer : layers)
	{
		bool found = false;

		for (uint32_t i = 0; i < layer_count; ++i)
		{
			if (strcmp(requested_layer, available_layers[i].layerName) == 0)
			{
				found = true;
				break;
			}
		}
		if (!found)
		{
			return false;
		}
	}

	return true;
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
	std::vector<const char*> debug_layers = {
		"VK_LAYER_KHRONOS_validation",
		// TODO: What's the difference? I believe above supersedes this.
		//"VK_LAYER_LUNARG_standard_validation",
		// TODO: Figure out how to use that.
		// "VK_LAYER_RENDERDOC_Capture",

	};
#ifdef _DEBUG
	// I don't think checking is required, it just won't be able to create the
	// instance if requested layers are unavailable.
	assert(CheckLayerSupport(debug_layers));

	instance_create_info.ppEnabledLayerNames = debug_layers.data();
	instance_create_info.enabledLayerCount = uint32_t(debug_layers.size());
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
	instance_create_info.enabledExtensionCount = ARRAY_SIZE(extensions);

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
	if (strstr(pMessage, "SPIR-V module not valid: Member index 0 is missing a location assignment"))
	{
		return VK_FALSE;
	}

	if ((flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT) || (flags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT))
	{
		return VK_FALSE;
	}

	const char* type = (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT)                                        ? "ERROR" :
			(flags & (VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT)) ? "WARNING" :
			(flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT)                                                   ? "DEBUG" :
                                                                                                        "INFO";

	char message[4096];
	snprintf(message, ARRAY_SIZE(message), "%s: %s\n\n", type, pMessage);

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
	if (strstr(pCallbackData->pMessage, "SPIR-V module not valid: Member index 0 is missing a location assignment"))
	{
		return VK_FALSE;
	}

	char prefix[64] = "";
	char* message = new char[strlen(pCallbackData->pMessage) + 5000];
	assert(message);

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

	if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
	{
		assert(!"Validation error!");
	}
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

static VkBool32 SupportsPresentation(VkPhysicalDevice physical_device, uint32_t family_index)
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
	VK_CHECK(vkEnumeratePhysicalDevices(instance, &physical_device_count, nullptr));
	std::vector<VkPhysicalDevice> physical_devices(physical_device_count);
	VK_CHECK(vkEnumeratePhysicalDevices(instance, &physical_device_count, physical_devices.data()));

	VkPhysicalDevice ret = VK_NULL_HANDLE;


	// Pick 1st discrete GPU.
	for (uint32_t i = 0; i < physical_device_count; ++i)
	{
		// VkPhysicalDeviceProperties props;
		// vkGetPhysicalDeviceProperties(physical_devices[i], &props);
		VkPhysicalDeviceProperties2 props2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
		VkPhysicalDeviceSubgroupProperties subgroup_props = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES };
		props2.pNext = &subgroup_props;
		vkGetPhysicalDeviceProperties2(physical_devices[i], &props2);

		VkPhysicalDeviceProperties& props = props2.properties;

		printf("GPU %u: %s.\n", i, props.deviceName);

		{  // Check for 8-bit int support.
			VkPhysicalDeviceFeatures2 features2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
			VkPhysicalDevice8BitStorageFeatures features_8bit = {
				VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES
			};
			// VkPhysicalDevice16BitStorageFeatures features_16bit = {
			//	VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES
			// };
			VkPhysicalDeviceVulkan11Features features_11 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
			VkPhysicalDeviceShaderFloat16Int8FeaturesKHR features_f16i8 = {
				VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES_KHR
			};
			// Don't pick device based on mesh shader capability. We
			// want that to be toggable at run-time and also support non-RTX cards.
			// That is now done in the main loop by enumerating the device extensions.
			// I guess checking the device features would be equally good/okay.
			// VkPhysicalDeviceMeshShaderFeaturesNV mesh_features = {
			// 	VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_NV
			// };

			features2.pNext = &features_8bit;
			// features_8bit.pNext = &features_16bit;
			features_8bit.pNext = &features_11;
			features_11.pNext = &features_f16i8;
			// features_16bit.pNext = &features_f16i8;
			//  features_f16i8.pNext = &mesh_features;
			vkGetPhysicalDeviceFeatures2(physical_devices[i], &features2);


			if (features_8bit.storageBuffer8BitAccess != VK_TRUE ||
					features_8bit.uniformAndStorageBuffer8BitAccess != VK_TRUE ||
					// features_16bit.storageBuffer16BitAccess != VK_TRUE ||
					features_11.storageBuffer16BitAccess == VK_TRUE || features_11.shaderDrawParameters == VK_TRUE ||
					features_f16i8.shaderFloat16 != VK_TRUE || features_f16i8.shaderInt8 != VK_TRUE
					/* || mesh_features.taskShader != VK_TRUE || mesh_features.meshShader != VK_TRUE */
			)
			{
				continue;
			}
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

		// TODO: Unclear if I check for enough/too much subgroup stuff.
		if (!(subgroup_props.supportedStages & VK_SHADER_STAGE_TASK_BIT_NV))
		{
			continue;
		}
		if (!(subgroup_props.supportedOperations & VK_SUBGROUP_FEATURE_ARITHMETIC_BIT) ||
				!(subgroup_props.supportedOperations & VK_SUBGROUP_FEATURE_BALLOT_BIT))
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

VkDevice CreateDevice(VkInstance instance, VkPhysicalDevice physical_device, uint32_t family_index, bool rtx_supported)
{
	assert(instance);
	assert(physical_device);

	const float queue_priorities[] = { 1.0f };

	VkDeviceQueueCreateInfo queue_create_info = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
	queue_create_info.queueFamilyIndex = family_index;
	queue_create_info.queueCount = 1;
	queue_create_info.pQueuePriorities = queue_priorities;

	std::vector<const char*> extensions = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
		VK_KHR_16BIT_STORAGE_EXTENSION_NAME,        // Using 16 bit in storage buffers
		VK_KHR_8BIT_STORAGE_EXTENSION_NAME,         // Using 8 bit in storage buffers
		VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME,  // Using 8/16 bit arithmetic in shaders
		// VK_EXT_SHADER_SUBGROUP_BALLOT_EXTENSION_NAME,  // I don't think this is necessary
		// VK_EXT_SHADER_SUBGROUP_VOTE_EXTENSION_NAME,  // I don't think this is necessary
	};
	if (rtx_supported)
	{
		extensions.push_back(VK_NV_MESH_SHADER_EXTENSION_NAME);
	}

	VkDeviceCreateInfo device_create_info = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	device_create_info.queueCreateInfoCount = 1;
	device_create_info.pQueueCreateInfos = &queue_create_info;
	device_create_info.ppEnabledExtensionNames = extensions.data();
	device_create_info.enabledExtensionCount = uint32_t(extensions.size());


	VkPhysicalDeviceFeatures2 features2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
	// VkPhysicalDeviceFeatures features = {};
	// features2.features.vertexPipelineStoresAndAtomics = VK_TRUE;	// TODO, for us it works, not for arseny.
	features2.features.multiDrawIndirect = VK_TRUE;

	VkPhysicalDevice8BitStorageFeatures features_8bit = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES };
	features_8bit.storageBuffer8BitAccess = VK_TRUE;
	features_8bit.uniformAndStorageBuffer8BitAccess = VK_TRUE;  // TODO: the above alone doesn't work, but this does.
	// VkPhysicalDevice16BitStorageFeatures features_16bit = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES
	// }; features_16bit.storageBuffer16BitAccess = VK_TRUE;

	VkPhysicalDeviceVulkan11Features features_11 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
	features_11.storageBuffer16BitAccess = VK_TRUE;
	features_11.shaderDrawParameters = VK_TRUE;

	VkPhysicalDeviceShaderFloat16Int8FeaturesKHR features_f16i8 = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES_KHR
	};
	features_f16i8.shaderFloat16 = VK_TRUE;
	features_f16i8.shaderInt8 = VK_TRUE;

	VkPhysicalDeviceMeshShaderFeaturesNV mesh_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_NV };
	mesh_features.taskShader = VK_TRUE;
	mesh_features.meshShader = VK_TRUE;

	// device_create_info.pEnabledFeatures = &features;
	device_create_info.pNext = &features2;
	features2.pNext = &features_8bit;
	// features_8bit.pNext = &features_16bit;
	features_8bit.pNext = &features_11;
	features_11.pNext = &features_f16i8;
	// features_16bit.pNext = &features_f16i8;
	if (rtx_supported)
	{
		features_f16i8.pNext = &mesh_features;
	}

	VkDevice device = VK_NULL_HANDLE;
	VK_CHECK(vkCreateDevice(physical_device, &device_create_info, nullptr, &device));

	return device;
}
