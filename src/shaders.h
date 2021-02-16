#pragma once

VkShaderModule LoadShader(VkDevice device, const char* path);

VkDescriptorSetLayout CreateDescriptorSetLayout(VkDevice device, bool rtx_enabled);

VkPipelineLayout CreatePipelineLayout(VkDevice device, VkDescriptorSetLayout set_layout);

VkDescriptorUpdateTemplate CreateUpdateTemplate(VkDevice device, VkPipelineBindPoint bind_point,
		VkDescriptorSetLayout set_layout, VkPipelineLayout pipeline_layout, bool rtx_enabled);

VkPipeline CreateGraphicsPipeline(VkDevice device, VkPipelineCache pipeline_cache, VkRenderPass render_pass,
		VkPipelineLayout layout, VkShaderModule vert_or_mesh, VkShaderModule frag, bool rtx_enabled);

struct DescriptorInfo
{
	union
	{
		VkDescriptorImageInfo image;
		VkDescriptorBufferInfo buffer;
	};

	DescriptorInfo(VkSampler sampler, VkImageView image_view, VkImageLayout image_layout)
	{
		image.sampler = sampler;
		image.imageView = image_view;
		image.imageLayout = image_layout;
	}

	//DescriptorInfo(VkBuffer buf, VkDeviceSize offset = 0, VkDeviceSize range = VK_WHOLE_SIZE)
	//{
	//	buffer.buffer = buf;
	//	buffer.offset = offset;
	//	buffer.range = range;
	//}

	DescriptorInfo(VkBuffer buf)
	{
		buffer.buffer = buf;
		buffer.offset = 0;
		buffer.range = VK_WHOLE_SIZE;
	}
};
