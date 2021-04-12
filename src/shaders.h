#pragma once

#include <initializer_list>

struct Shader
{
	VkShaderModule module;
	VkShaderStageFlagBits stage;
	uint32_t storage_buffer_mask;
	bool uses_push_constants;
};

struct Program
{
	VkDescriptorSetLayout descriptor_set_layout;
	VkPipelineLayout pipeline_layout;
	VkDescriptorUpdateTemplate descriptor_update_template;
	VkShaderStageFlags push_constant_stages;
};

bool LoadShader(Shader& shader, VkDevice device, const char* path);
void DestroyShader(Shader& shader, VkDevice device);

using Shaders = std::initializer_list<const Shader*>;
// TODO: Should Shaders be passed as value or reference?

Program CreateProgram(VkDevice device, VkPipelineBindPoint bind_point, Shaders shaders, size_t push_constant_size);
void DestroyProgram(VkDevice device, Program& program);

VkPipeline CreateGraphicsPipeline(VkDevice device, VkPipelineCache pipeline_cache, VkRenderPass render_pass,
		VkPipelineLayout layout, Shaders shaders);

struct DescriptorInfo
{
	union
	{
		VkDescriptorImageInfo image;
		VkDescriptorBufferInfo buffer;
	};

	DescriptorInfo() {}

	DescriptorInfo(VkSampler sampler, VkImageView image_view, VkImageLayout image_layout)
	{
		image.sampler = sampler;
		image.imageView = image_view;
		image.imageLayout = image_layout;
	}

	DescriptorInfo(VkBuffer buf, VkDeviceSize offset = 0, VkDeviceSize range = VK_WHOLE_SIZE)
	{
		buffer.buffer = buf;
		buffer.offset = offset;
		buffer.range = range;
	}

	// DescriptorInfo(VkBuffer buf)
	//{
	//	buffer.buffer = buf;
	//	buffer.offset = 0;
	//	buffer.range = VK_WHOLE_SIZE;
	//}
};
