// Disable warnings for unscoped enums, the Vulkan headers are full of them.
#pragma warning(disable : 26812)

// Uninitialized members
#pragma warning(disable : 26495)

#include <algorithm>

#include <fast_obj.h>
#include <meshoptimizer.h>
#include <volk.h>
#include <GLFW/glfw3.h>

#include "common.h"

#include "device.h"
#include "resources.h"
#include "shaders.h"
#include "swapchain.h"

// Prevent warning from glm includes. Compiler bug, see here:
// https://developercommunity.visualstudio.com/t/warning-c4103-in-visual-studio-166-update/1057589
#pragma warning(push)
#pragma warning(disable : 4103)
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/quaternion_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#pragma warning(pop)

VkSemaphore CreateSemaphore(VkDevice device);
VkRenderPass CreateRenderPass(VkDevice device, VkFormat color_format, VkFormat depth_format);
VkFramebuffer CreateFrameBuffer(VkDevice device, VkRenderPass render_pass, VkImageView color_view,
		VkImageView depth_view, uint32_t width, uint32_t height);
VkCommandPool CreateCommandBufferPool(VkDevice device, uint32_t family_index);

struct Vertex
{
	// TODO: Do this switch optionally, via flag.
	float vx, vy, vz;
	// uint16_t vx, vy, vz, vw;
	// float nx, ny, nz;
	uint8_t nx, ny, nz, nw;
	// float tu, tv;
	uint16_t tu, tv;
};

struct alignas(16) Meshlet
{
	glm::vec3 center;
	float radius;
	int8_t cone_axis[3];
	int8_t cone_cutoff;
	// glm::vec3 cone_apex;
	// float padding;

	// [data_offset, (data_offset + vertex_count - 1)] stores vertex indices
	// [(data_offset + vertex_count), (data_offset + vertex_count + index_count)] stores packed 4b meshlet indices
	uint32_t data_offset;

	// OLD
	// uint32_t vertices[64];
	// gl_PrimitiveCountNV + gl_PrimitiveINdicesNV[]
	// OLD: // together should take no more than 128 bytes, hence 42 triangles + count.
	// together they up a multiple of 128 bytes, indices take bytes, the count 4 bytes (wtf, why?), hence 126
	// triangles / + count. We lower to 124 triangles for a divisibility by 4.
	// uint8_t indices[124 * 3];

	uint8_t vertex_count;
	uint8_t triangle_count;
};

struct alignas(16) Globals
{
	glm::mat4 projection;
};

struct alignas(16) MeshDraw
{
	glm::vec3 position;
	float scale;
	glm::quat orientation;

	union
	{
		uint32_t command_data[7];

		struct
		{
			VkDrawIndexedIndirectCommand command_indirect;         // 5 u32s
			VkDrawMeshTasksIndirectCommandNV command_indirect_ms;  // 2 u32s
		};
	};
};

struct Mesh
{
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	std::vector<Meshlet> meshlets;
	std::vector<uint32_t> meshlet_data;
};

bool mesh_shading_supported = false;
bool mesh_shading_enabled = false;

static bool LoadMesh(Mesh& result, const char* path)
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
			// v.vx = meshopt_quantizeHalf(obj->positions[idx.p * 3 + 0]);
			// v.vy = meshopt_quantizeHalf(obj->positions[idx.p * 3 + 1]);
			// v.vz = meshopt_quantizeHalf(obj->positions[idx.p * 3 + 2]);
			// TODO: Fix rounding.
			v.nx = uint8_t(nx * 127.0f + 127.0f);
			v.ny = uint8_t(ny * 127.0f + 127.0f);
			v.nz = uint8_t(nz * 127.0f + 127.0f);
			// v.tu = obj->texcoords[idx.t * 3 + 0];
			// v.tv = obj->texcoords[idx.t * 3 + 1];
			v.tu = meshopt_quantizeHalf(obj->texcoords[idx.t * 3 + 0]);
			v.tv = meshopt_quantizeHalf(obj->texcoords[idx.t * 3 + 1]);
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

		const bool kSimulateShittyOrdering = false;
		if (kSimulateShittyOrdering)
		{
			struct Triangle
			{
				unsigned int v[3];
			};
			std::random_shuffle((Triangle*)result.indices.data(), (Triangle*)(result.indices.data() + index_count));
		}

		// Optimize mesh for more efficient GPU rendering.
		const bool kOptimizeVertexCache = true;
		if (kOptimizeVertexCache)
		{
			meshopt_optimizeVertexCache(
					result.indices.data(), result.indices.data(), index_count, unique_vertices_count);
			meshopt_optimizeVertexFetch(result.vertices.data(), result.indices.data(), index_count,
					result.vertices.data(), unique_vertices_count, sizeof(Vertex));
		}
	}

	return true;
}

static void BuildMeshlets(Mesh& mesh)
{
	const size_t kMaxVertices = 64;
	const size_t kMaxTriangles = 124;

	std::vector<meshopt_Meshlet> meshlets(meshopt_buildMeshletsBound(mesh.indices.size(), kMaxVertices, kMaxTriangles));
	meshlets.resize(meshopt_buildMeshlets(meshlets.data(), mesh.indices.data(), mesh.indices.size(),
			mesh.vertices.size(), kMaxVertices, kMaxTriangles));

	// TODO: We don't really need this, but this way we can guarantee that every
	// thread in a warp accesses valid data. Once we have to push constants, we
	// can then add the check.
	while (meshlets.size() % 32 != 0)
	{
		meshlets.push_back(meshopt_Meshlet());  // I assume this 0-inits the counts.
	}

	mesh.meshlets.resize(meshlets.size());
	for (size_t i = 0; i < meshlets.size(); ++i)
	{
		const meshopt_Meshlet& meshlet = meshlets[i];

		const uint32_t data_offset = (uint32_t)mesh.meshlet_data.size();

		// for (size_t j = 0; j < meshlet.vertex_count; ++j)
		//{
		//	mesh.meshlet_data.push_back(meshlet.vertices[j]);
		//}
		mesh.meshlet_data.insert(mesh.meshlet_data.end(), meshlet.vertices, meshlet.vertices + meshlet.vertex_count);

		const size_t index_group_count = (meshlet.triangle_count * 3 + 3) / 4;
		// uint32_t index_groups[(kMaxTriangles * 3 + 3) / 4] = {};
		// memcpy(index_groups, meshlet.indices, meshlet.triangle_count * 3);
		const uint32_t* index_groups = reinterpret_cast<const uint32_t*>(meshlet.indices);
		mesh.meshlet_data.insert(mesh.meshlet_data.end(), index_groups, index_groups + index_group_count);

		const meshopt_Bounds bounds =
				meshopt_computeMeshletBounds(&meshlet, &mesh.vertices[0].vx, mesh.vertices.size(), sizeof(Vertex));

		Meshlet m = {};
		m.data_offset = data_offset;
		m.vertex_count = meshlet.vertex_count;
		m.triangle_count = meshlet.triangle_count;

		m.center = glm::vec3(bounds.center[0], bounds.center[1], bounds.center[2]);
		m.radius = bounds.radius;
		// m.cone_axis = glm::vec3(bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2]);
		// m.cone_cutoff = bounds.cone_cutoff;
		m.cone_axis[0] = bounds.cone_axis_s8[0];
		m.cone_axis[1] = bounds.cone_axis_s8[1];
		m.cone_axis[2] = bounds.cone_axis_s8[2];
		m.cone_cutoff = bounds.cone_cutoff_s8;
		// m.cone_apex = glm::vec3(bounds.cone_apex[0], bounds.cone_apex[1], bounds.cone_apex[2]);
		// m.padding = 0;

		mesh.meshlets[i] = m;
	}
}

// TODO: Check if timing/querying capability is available.
VkQueryPool CreateQueryPool(VkDevice device, uint32_t pool_size)

{
	VkQueryPoolCreateInfo query_pool_create_info = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
	// query_pool_create_info.flags;
	query_pool_create_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
	query_pool_create_info.queryCount = pool_size;
	// query_pool_create_info.pipelineStatistics;

	VkQueryPool query_pool = VK_NULL_HANDLE;
	VK_CHECK(vkCreateQueryPool(device, &query_pool_create_info, nullptr, &query_pool));

	return query_pool;
}


void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
	{
		glfwSetWindowShouldClose(window, GLFW_TRUE);
	}
	else if (key == GLFW_KEY_SPACE && action == GLFW_PRESS)
	{
		mesh_shading_enabled = (!mesh_shading_enabled) && mesh_shading_supported;
	}
}

glm::mat4 ReverseInfiniteProjectionRightHandedWithoutEpsilon(float fovy_radians, float aspect_w_by_h, float z_near)
{
	float f = 1.0f / tanf(fovy_radians / 2.0f);
	return glm::mat4(
			// clang-format off
			f / aspect_w_by_h, 0.0f,   0.0f,  0.0f,
			             0.0f,    f,   0.0f,  0.0f,
			             0.0f, 0.0f,   0.0f, -1.0f,  // From RH -> LH: remove -
			             0.0f, 0.0f, z_near,  0.0f
			// clang-format on
	);
}

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

	// volkLoadInstance(instance);
	volkLoadInstanceOnly(instance);

#ifdef _DEBUG
	// VkDebugReportCallbackEXT debug_callback = RegisterDebugCallback(instance);
	// assert(debug_callback);
	VkDebugUtilsMessengerEXT debug_messenger = RegisterDebugUtilsMessenger(instance);
	assert(debug_messenger);
#endif

	VkPhysicalDevice physical_device = PickPhysicalDevice(instance);
	assert(physical_device);

	uint32_t extension_count = 0;
	VK_CHECK(vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &extension_count, nullptr));
	std::vector<VkExtensionProperties> extensions(extension_count);
	VK_CHECK(vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &extension_count, extensions.data()));
	mesh_shading_supported = false;
	for (const auto& ext : extensions)
	{
		if (strcmp(ext.extensionName, "VK_NV_mesh_shader") == 0)
		{
			mesh_shading_supported = true;
			break;
		}
	}
	mesh_shading_enabled = mesh_shading_supported;

	VkPhysicalDeviceProperties physical_device_props = {};
	vkGetPhysicalDeviceProperties(physical_device, &physical_device_props);
	// TODO: put into PickPhysicalDevice
	assert(physical_device_props.limits.timestampComputeAndGraphics);


	const uint32_t family_index = GetGraphicsFamilyIndex(physical_device);
	assert(family_index != VK_QUEUE_FAMILY_IGNORED);

	VkDevice device = CreateDevice(instance, physical_device, family_index, mesh_shading_supported);
	assert(device);

	volkLoadDevice(device);

	const int window_width = 1024 * 2;
	const int window_height = 768 * 2;
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // For NVidia
	GLFWwindow* window = glfwCreateWindow(window_width, window_height, "Hello Vulkan", nullptr, nullptr);
	assert(window);
	glfwSetKeyCallback(window, KeyCallback);

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

	VkRenderPass render_pass = CreateRenderPass(device, swapchain_format, VK_FORMAT_D32_SFLOAT);
	assert(render_pass);

	// NOTE: This is earlier here than what Arseny is doing.
	Swapchain swapchain;
	CreateSwapchain(
			physical_device, device, surface, swapchain_format, family_index, render_pass, VK_NULL_HANDLE, swapchain);

	VkQueryPool query_pool = CreateQueryPool(device, 128);
	assert(query_pool);

	Shader meshlet_mesh = {};
	Shader meshlet_task = {};
	if (mesh_shading_supported)
	{
		bool rc;
		rc = LoadShader(meshlet_mesh, device, "meshlet.mesh.spv");
		assert(rc);
		rc = LoadShader(meshlet_task, device, "meshlet.task.spv");
		assert(rc);
	}
	Shader mesh_vert = {};
	Shader mesh_frag = {};
	{
		bool rc;
		rc = LoadShader(mesh_vert, device, "mesh.vert.spv");
		assert(rc);
		rc = LoadShader(mesh_frag, device, "mesh.frag.spv");
		assert(rc);
	}

	VkPipelineCache pipeline_cache = VK_NULL_HANDLE;

	Shaders mesh_shaders = { &mesh_vert, &mesh_frag };
	Shaders meshlet_shaders = { &meshlet_task, &meshlet_mesh, &mesh_frag };

	Program mesh_program = CreateProgram(device, VK_PIPELINE_BIND_POINT_GRAPHICS, mesh_shaders, sizeof(Globals));
	VkPipeline mesh_pipeline =
			CreateGraphicsPipeline(device, pipeline_cache, render_pass, mesh_program.pipeline_layout, mesh_shaders);
	assert(mesh_pipeline);

	Program meshlet_program = {};
	VkPipeline meshlet_pipeline = VK_NULL_HANDLE;
	if (mesh_shading_supported)
	{
		meshlet_program = CreateProgram(device, VK_PIPELINE_BIND_POINT_GRAPHICS, meshlet_shaders, sizeof(Globals));
		meshlet_pipeline = CreateGraphicsPipeline(
				device, pipeline_cache, render_pass, meshlet_program.pipeline_layout, meshlet_shaders);
		assert(meshlet_pipeline);
	}

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

	if (mesh_shading_supported)
	{
		BuildMeshlets(mesh);
	}

	Buffer scratch_buffer = {};
	CreateBuffer(scratch_buffer, device, memory_properties, 128 * 1024 * 1024, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	Buffer vertex_buffer = {};
	CreateBuffer(vertex_buffer, device, memory_properties, 128 * 1024 * 1024,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	Buffer index_buffer = {};
	CreateBuffer(index_buffer, device, memory_properties, 128 * 1024 * 1024,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	Buffer meshlet_buffer = {};
	Buffer meshlet_data_buffer = {};
	if (mesh_shading_supported)
	{
		CreateBuffer(meshlet_buffer, device, memory_properties, 128 * 1024 * 1024,
				VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		CreateBuffer(meshlet_data_buffer, device, memory_properties, 128 * 1024 * 1024,
				VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	}

	assert(vertex_buffer.size >= mesh.vertices.size() * sizeof(Vertex));
	UploadBuffer(device, cmd_buf_pool, cmd_buf, queue, vertex_buffer, scratch_buffer, mesh.vertices.data(),
			mesh.vertices.size() * sizeof(mesh.vertices[0]));
	assert(index_buffer.size >= mesh.indices.size() * sizeof(uint32_t));
	UploadBuffer(device, cmd_buf_pool, cmd_buf, queue, index_buffer, scratch_buffer, mesh.indices.data(),
			mesh.indices.size() * sizeof(mesh.indices[0]));
	if (mesh_shading_supported)
	{
		assert(meshlet_buffer.size >= mesh.meshlets.size() * sizeof(Meshlet));
		UploadBuffer(device, cmd_buf_pool, cmd_buf, queue, meshlet_buffer, scratch_buffer, mesh.meshlets.data(),
				mesh.meshlets.size() * sizeof(mesh.meshlets[0]));
		UploadBuffer(device, cmd_buf_pool, cmd_buf, queue, meshlet_data_buffer, scratch_buffer,
				mesh.meshlet_data.data(), mesh.meshlet_data.size() * sizeof(mesh.meshlet_data[0]));
	}

	size_t draw_count = 3000;
	std::vector<MeshDraw> draws(draw_count);
	for (uint32_t i = 0; i < draw_count; ++i)
	{
		draws[i].position[0] = (float(rand()) / RAND_MAX) * 40.0f - 20.0f;
		draws[i].position[1] = (float(rand()) / RAND_MAX) * 40.0f - 20.0f;
		draws[i].position[2] = (float(rand()) / RAND_MAX) * 40.0f - 20.0f;
		draws[i].scale = float(rand()) / RAND_MAX * 2.9f + 0.1f;

		const glm::vec3 axis(float(rand()) / RAND_MAX * 2.0f - 1.0f, float(rand()) / RAND_MAX * 2.0f - 1.0f,
				float(rand()) / RAND_MAX * 2.0f - 1.0f);
		const float angle = glm::radians(float(rand()) / RAND_MAX * 90.0f);
		draws[i].orientation = glm::rotate(glm::quat(1.0f, 0.0f, 0.0f, 0.0f), angle, axis);

		memset(draws[i].command_data, 0, sizeof(draws[i].command_data));
		draws[i].command_indirect.indexCount = uint32_t(mesh.indices.size());
		draws[i].command_indirect.instanceCount = 1;
		draws[i].command_indirect_ms.taskCount = uint32_t(mesh.meshlets.size()) / 32;
	}

	Buffer draw_buffer = {};
	CreateBuffer(draw_buffer, device, memory_properties, 128 * 1024 * 1024,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	UploadBuffer(device, cmd_buf_pool, cmd_buf, queue, draw_buffer, scratch_buffer, draws.data(),
			draws.size() * sizeof(draws[0]));


	Image color_target = {};
	Image depth_target = {};
	VkFramebuffer target_fb = VK_NULL_HANDLE;

	double frame_avg_cpu = 0.0;
	double frame_avg_gpu = 0.0;

	while (!glfwWindowShouldClose(window))
	{
		const double frame_begin_cpu = glfwGetTime() * 1000.0;

		glfwPollEvents();

		if (ResizeSwapchainIfNecessary(
					physical_device, device, surface, swapchain_format, family_index, render_pass, swapchain) ||
				!target_fb)
		{
			if (target_fb)
			{
				DestroyImage(device, color_target);
				DestroyImage(device, depth_target);
				vkDestroyFramebuffer(device, target_fb, nullptr);
			}
			color_target = CreateImage(device, memory_properties, swapchain.width, swapchain.height, 1,
					swapchain_format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
			depth_target = CreateImage(device, memory_properties, swapchain.width, swapchain.height, 1,
					VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
			target_fb = CreateFrameBuffer(device, render_pass, color_target.image_view, depth_target.image_view,
					swapchain.width, swapchain.height);
		}

		uint32_t image_index = 0;
		VK_CHECK(vkAcquireNextImageKHR(
				device, swapchain.swapchain, ~0ull, aquire_semaphore, VK_NULL_HANDLE, &image_index));

		VK_CHECK(vkResetCommandPool(device, cmd_buf_pool, 0));

		VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		VK_CHECK(vkBeginCommandBuffer(cmd_buf, &begin_info));

		vkCmdResetQueryPool(cmd_buf, query_pool, 0, 128);
		vkCmdWriteTimestamp(cmd_buf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query_pool, 0);


		// TODO: I feel this is wrong and the dst access flags should be
		// 1. VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
		// 2. VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
		VkImageMemoryBarrier render_begin_barriers[] = {
			ImageBarrier(color_target.image, 0, 0, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					VK_IMAGE_ASPECT_COLOR_BIT),
			ImageBarrier(depth_target.image, 0, 0, VK_IMAGE_LAYOUT_UNDEFINED,
					VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT),
		};
		vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
						VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
				VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 0, nullptr, ARRAY_SIZE(render_begin_barriers),
				render_begin_barriers);

		VkClearValue clear_values[2];
		clear_values[0].color = { 48.0f / 255.0f, 10.0f / 255.0f, 36.0f / 255.0f, 1.0f };  // Ubuntu terminal color.
		clear_values[1].depthStencil = { 0.0f };

		VkRenderPassBeginInfo pass_begin_info = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
		pass_begin_info.renderPass = render_pass;
		// pass_begin_info.framebuffer = swapchain.framebuffers[image_index];
		pass_begin_info.framebuffer = target_fb;
		pass_begin_info.renderArea.extent.width = swapchain.width;
		pass_begin_info.renderArea.extent.height = swapchain.height;
		pass_begin_info.clearValueCount = ARRAY_SIZE(clear_values);
		pass_begin_info.pClearValues = clear_values;

		vkCmdBeginRenderPass(cmd_buf, &pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport viewport = { 0.0f, (float)swapchain.height, (float)swapchain.width, -(float)swapchain.height, 0.0f,
			1.0f };
		VkRect2D scissor = { { 0, 0 }, { uint32_t(swapchain.width), uint32_t(swapchain.height) } };

		vkCmdSetViewport(cmd_buf, 0, 1, &viewport);
		vkCmdSetScissor(cmd_buf, 0, 1, &scissor);

		// Descriptor set binding is a good match for AMD, but not for NVidia (and likely neither for Intel).
		// We won't use descriptor set binding, we'll use an extension exposed by Intel and NVidia only.
		// They are like push constants, but for descriptor sets.

		const glm::mat4 projection = ReverseInfiniteProjectionRightHandedWithoutEpsilon(
				glm::radians(70.0f), float(swapchain.width) / float(swapchain.height), 0.01f);

		Globals globals = {};
		globals.projection = projection;

		if (mesh_shading_enabled)
		{
			vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, meshlet_pipeline);

			DescriptorInfo descriptors[] = {
				draw_buffer.buffer,
				meshlet_buffer.buffer,
				meshlet_data_buffer.buffer,
				vertex_buffer.buffer,
			};
			vkCmdPushDescriptorSetWithTemplateKHR(cmd_buf, meshlet_program.descriptor_update_template,
					meshlet_program.pipeline_layout, 0, descriptors);

			vkCmdPushConstants(cmd_buf, meshlet_program.pipeline_layout, meshlet_program.push_constant_stages, 0,
					sizeof(globals), &globals);

			vkCmdDrawMeshTasksIndirectNV(cmd_buf, draw_buffer.buffer, offsetof(MeshDraw, command_indirect_ms),
					uint32_t(draws.size()), sizeof(MeshDraw));
		}
		else
		{
			vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, mesh_pipeline);

			DescriptorInfo descriptors[] = {
				draw_buffer.buffer,
				vertex_buffer.buffer,
			};
			vkCmdPushDescriptorSetWithTemplateKHR(
					cmd_buf, mesh_program.descriptor_update_template, mesh_program.pipeline_layout, 0, descriptors);

			vkCmdBindIndexBuffer(cmd_buf, index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);

			vkCmdPushConstants(cmd_buf, mesh_program.pipeline_layout, mesh_program.push_constant_stages, 0,
					sizeof(globals), &globals);

			vkCmdDrawIndexedIndirect(cmd_buf, draw_buffer.buffer, offsetof(MeshDraw, command_indirect),
					uint32_t(draws.size()), sizeof(MeshDraw));
		}

		vkCmdEndRenderPass(cmd_buf);

		// NOTE: Likely a VKSubpassDependency could be used here instead of the barrier. This is explained in:
		// https://themaister.net/blog/2019/08/14/yet-another-blog-explaining-vulkan-synchronization/
		VkImageMemoryBarrier copy_barriers[] = {
			ImageBarrier(swapchain.images[image_index], 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT),
			ImageBarrier(color_target.image, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
					VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					VK_IMAGE_ASPECT_COLOR_BIT),
		};
		vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 0, nullptr, ARRAY_SIZE(copy_barriers), copy_barriers);

		VkImageCopy copy_region = {};
		copy_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copy_region.srcSubresource.layerCount = 1;
		copy_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copy_region.dstSubresource.layerCount = 1;
		copy_region.extent = { swapchain.width, swapchain.height, 1 };
		vkCmdCopyImage(cmd_buf, color_target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapchain.images[image_index],
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

		VkImageMemoryBarrier present_barrier = ImageBarrier(swapchain.images[image_index], VK_ACCESS_TRANSFER_WRITE_BIT,
				0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_ASPECT_COLOR_BIT);
		vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
				VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 0, nullptr, 1, &present_barrier);

		vkCmdWriteTimestamp(cmd_buf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query_pool, 1);
		VK_CHECK(vkEndCommandBuffer(cmd_buf));

		VkPipelineStageFlags submit_stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT;

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
		double wait_begin = glfwGetTime() * 1000.0;
		VK_CHECK(vkDeviceWaitIdle(device));
		double wait_end = glfwGetTime() * 1000.0;

		{  //  Profiling
			uint64_t query_results[2];
			VK_CHECK(vkGetQueryPoolResults(device, query_pool, 0, ARRAY_SIZE(query_results), sizeof(query_results),
					query_results, sizeof(query_results[0]), VK_QUERY_RESULT_64_BIT));

			const double frame_begin_gpu =
					double(query_results[0]) * physical_device_props.limits.timestampPeriod * 1e-6;
			const double frame_end_gpu = double(query_results[1]) * physical_device_props.limits.timestampPeriod * 1e-6;

			const double frame_end_cpu = glfwGetTime() * 1000.0;

			frame_avg_cpu = frame_avg_cpu * 0.95 + (frame_end_cpu - frame_begin_cpu) * 0.05;
			frame_avg_gpu = frame_avg_gpu * 0.95 + (frame_end_gpu - frame_begin_gpu) * 0.05;

			const double tris_per_sec = double(draw_count) * double(mesh.indices.size() / 3) / (frame_avg_gpu * 1e-3);
			const double kitens_per_sec = double(draw_count) / (frame_avg_gpu * 1e-3);

			char title[256];
			sprintf(title,
					"%s; CPU: %.1f ms; wait %.2f ms; GPU: %.3f ms; triangles %d; meshlets %d; %.2fB tris/s, %.1fM "
					"kittens/s",
					mesh_shading_enabled ? "RTX" : "non-RTX", frame_avg_cpu, (wait_end - wait_begin), frame_avg_gpu,
					(int)(mesh.indices.size() / 3), (int)(mesh.meshlets.size()), tris_per_sec * 1e-9f,
					kitens_per_sec * 1e-6f);
			glfwSetWindowTitle(window, title);
		}
	}

	VK_CHECK(vkDeviceWaitIdle(device));

	vkDestroyFramebuffer(device, target_fb, nullptr);
	DestroyImage(device, depth_target);
	DestroyImage(device, color_target);

	DestroyBuffer(draw_buffer, device);

	if (mesh_shading_supported)
	{
		DestroyBuffer(meshlet_buffer, device);
		DestroyBuffer(meshlet_data_buffer, device);
	}
	DestroyBuffer(vertex_buffer, device);
	DestroyBuffer(index_buffer, device);
	DestroyBuffer(scratch_buffer, device);

	vkDestroyCommandPool(device, cmd_buf_pool, nullptr);

	vkDestroyPipeline(device, mesh_pipeline, nullptr);
	DestroyProgram(device, mesh_program);

	if (mesh_shading_supported)
	{
		vkDestroyPipeline(device, meshlet_pipeline, nullptr);
		DestroyProgram(device, meshlet_program);
	}

	// vkDestroyPipelineCache(device, pipeline_cache, nullptr);

	DestroyShader(mesh_frag, device);
	DestroyShader(mesh_vert, device);

	if (mesh_shading_supported)
	{
		DestroyShader(meshlet_mesh, device);
		DestroyShader(meshlet_task, device);
	}

	vkDestroyQueryPool(device, query_pool, nullptr);

	DestroySwapchain(device, swapchain);

	vkDestroyRenderPass(device, render_pass, nullptr);

	vkDestroySemaphore(device, release_semaphore, nullptr);
	vkDestroySemaphore(device, aquire_semaphore, nullptr);

	vkDestroySurfaceKHR(instance, surface, nullptr);

	glfwDestroyWindow(window);

	vkDestroyDevice(device, nullptr);

#ifdef _DEBUG
	// vkDestroyDebugReportCallbackEXT(instance, debug_callback, nullptr);
	vkDestroyDebugUtilsMessengerEXT(instance, debug_messenger, nullptr);
#endif
	vkDestroyInstance(instance, nullptr);

	return 0;
}

VkSemaphore CreateSemaphore(VkDevice device)
{
	assert(device);
	VkSemaphoreCreateInfo semaphore_create_info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

	VkSemaphore semaphore = VK_NULL_HANDLE;
	VK_CHECK(vkCreateSemaphore(device, &semaphore_create_info, nullptr, &semaphore));

	return semaphore;
}

VkRenderPass CreateRenderPass(VkDevice device, VkFormat color_format, VkFormat depth_format)
{
	assert(device);
	assert(color_format);
	assert(depth_format);

	VkAttachmentDescription attachments[2] = {};
	attachments[0].format = color_format;
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	attachments[1].format = depth_format;
	attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference color_attachment_ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
	VkAttachmentReference depth_attachment_ref = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_attachment_ref;
	subpass.pDepthStencilAttachment = &depth_attachment_ref;

	VkRenderPassCreateInfo pass_create_info = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
	pass_create_info.attachmentCount = ARRAY_SIZE(attachments);
	pass_create_info.pAttachments = attachments;
	pass_create_info.subpassCount = 1;
	pass_create_info.pSubpasses = &subpass;

	VkRenderPass render_pass = VK_NULL_HANDLE;
	VK_CHECK(vkCreateRenderPass(device, &pass_create_info, nullptr, &render_pass));
	return render_pass;
}

VkFramebuffer CreateFrameBuffer(VkDevice device, VkRenderPass render_pass, VkImageView color_view,
		VkImageView depth_view, uint32_t width, uint32_t height)
{
	assert(device);
	assert(render_pass);

	VkImageView attachments[] = { color_view, depth_view };

	VkFramebufferCreateInfo fb_create_info = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
	fb_create_info.renderPass = render_pass;
	fb_create_info.attachmentCount = ARRAY_SIZE(attachments);
	fb_create_info.pAttachments = attachments;
	fb_create_info.width = width;
	fb_create_info.height = height;
	fb_create_info.layers = 1;

	VkFramebuffer framebuffer = VK_NULL_HANDLE;
	VK_CHECK(vkCreateFramebuffer(device, &fb_create_info, nullptr, &framebuffer));
	return framebuffer;
};

VkCommandPool CreateCommandBufferPool(VkDevice device, uint32_t family_index)
{
	assert(device);
	VkCommandPoolCreateInfo cmd_pool_create_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	cmd_pool_create_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	cmd_pool_create_info.queueFamilyIndex = family_index;

	VkCommandPool cmd_pool = VK_NULL_HANDLE;
	VK_CHECK(vkCreateCommandPool(device, &cmd_pool_create_info, nullptr, &cmd_pool));

	return cmd_pool;
}
