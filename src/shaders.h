#pragma once

struct Shader
{
	VkShaderModule module;
	VkShaderStageFlagBits stage;
	uint32_t storage_buffer_mask;
};

bool LoadShader(Shader& shader, VkDevice device, const char* path);
void DestroyShader(Shader& shader, VkDevice device);

VkDescriptorSetLayout CreateDescriptorSetLayout(VkDevice device, const Shader& vert_or_mesh, const Shader& frag);

VkPipelineLayout CreatePipelineLayout(VkDevice device, VkDescriptorSetLayout set_layout);

VkDescriptorUpdateTemplate
CreateUpdateTemplate(VkDevice device, VkPipelineBindPoint bind_point, VkDescriptorSetLayout set_layout,
		VkPipelineLayout pipeline_layout, const Shader& vert_or_mesh, const Shader& frag);

VkPipeline CreateGraphicsPipeline(VkDevice device, VkPipelineCache pipeline_cache, VkRenderPass render_pass,
		VkPipelineLayout layout, const Shader& vert_or_mesh, const Shader& frag);

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
