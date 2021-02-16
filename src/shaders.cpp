#include "common.h"
#include "shaders.h"

#include <stdio.h>

#include <vector>

VkShaderModule LoadShader(VkDevice device, const char* path)
{
	assert(device);

	FILE* file = fopen(path, "rb");
	assert(file);
	fseek(file, 0, SEEK_END);
	const long length = ftell(file);
	assert(length >= 0);
	assert(length % 4 == 0);
	fseek(file, 0, SEEK_SET);

	char* buffer = (char*)malloc(length);
	assert(buffer);

	const size_t rc = fread(buffer, 1, length, file);
	assert(rc == length);
	fclose(file);

	VkShaderModuleCreateInfo shader_create_info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	shader_create_info.codeSize = length;
	shader_create_info.pCode = (const uint32_t*)buffer;

	VkShaderModule shader_module = VK_NULL_HANDLE;
	VK_CHECK(vkCreateShaderModule(device, &shader_create_info, nullptr, &shader_module));

	free(buffer);
	return shader_module;
}

VkDescriptorSetLayout CreateDescriptorSetLayout(VkDevice device, bool rtx_enabled)
{
	// TODO extract from SPIR-V
	std::vector<VkDescriptorSetLayoutBinding> set_layout_bindings;
	if (rtx_enabled)
	{
		set_layout_bindings.resize(2);
		set_layout_bindings[0].binding = 0;
		set_layout_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		set_layout_bindings[0].descriptorCount = 1;
		set_layout_bindings[0].stageFlags = VK_SHADER_STAGE_MESH_BIT_NV;
		set_layout_bindings[1].binding = 1;
		set_layout_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		set_layout_bindings[1].descriptorCount = 1;
		set_layout_bindings[1].stageFlags = VK_SHADER_STAGE_MESH_BIT_NV;
	}
	else
	{
		set_layout_bindings.resize(1);
		set_layout_bindings[0].binding = 0;
		set_layout_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		set_layout_bindings[0].descriptorCount = 1;
		set_layout_bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	}

	VkDescriptorSetLayoutCreateInfo set_layout_create_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	// I guess normally we'd go with VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT.
	// But since we're using the push extensions, it's like this:
	set_layout_create_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
	set_layout_create_info.bindingCount = uint32_t(set_layout_bindings.size());
	set_layout_create_info.pBindings = set_layout_bindings.data();

	VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
	VK_CHECK(vkCreateDescriptorSetLayout(device, &set_layout_create_info, nullptr, &set_layout));

	return set_layout;
}

VkPipelineLayout CreatePipelineLayout(VkDevice device, VkDescriptorSetLayout set_layout)
{
	VkPipelineLayoutCreateInfo layout_create_info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	layout_create_info.setLayoutCount = 1;
	layout_create_info.pSetLayouts = &set_layout;
	// layout_create_info.pushConstantRangeCount;
	// layout_create_info.pPushConstantRanges;

	VkPipelineLayout layout = VK_NULL_HANDLE;
	VK_CHECK(vkCreatePipelineLayout(device, &layout_create_info, nullptr, &layout));

	return layout;
}

VkDescriptorUpdateTemplate CreateUpdateTemplate(VkDevice device, VkPipelineBindPoint bind_point,
		VkDescriptorSetLayout set_layout, VkPipelineLayout pipeline_layout, bool rtx_enabled)
{
	assert(device);

	std::vector<VkDescriptorUpdateTemplateEntry> entries;
	if (rtx_enabled)
	{
		// TODO wouldn't it be better to use 1 descriptor set with 2 descriptors?
		entries.resize(2);
		entries[0].dstBinding = 0;
		entries[0].dstArrayElement = 0;
		entries[0].descriptorCount = 1;
		entries[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		entries[0].offset = sizeof(DescriptorInfo) * 0;
		entries[0].stride = sizeof(DescriptorInfo);
		entries[1].dstBinding = 1;
		entries[1].dstArrayElement = 0;
		entries[1].descriptorCount = 1;
		entries[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		entries[1].offset = sizeof(DescriptorInfo) * 1;
		entries[1].stride = sizeof(DescriptorInfo);
	}
	else
	{
		entries.resize(1);
		entries[0].dstBinding = 0;
		entries[0].dstArrayElement = 0;
		entries[0].descriptorCount = 1;
		entries[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		entries[0].offset = sizeof(DescriptorInfo) * 0;
		entries[0].stride = sizeof(DescriptorInfo);
	}

	VkDescriptorUpdateTemplateCreateInfo template_create_info = {
		VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO
	};
	template_create_info.flags;
	template_create_info.descriptorUpdateEntryCount = uint32_t(entries.size());
	template_create_info.pDescriptorUpdateEntries = entries.data();
	template_create_info.templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR;
	template_create_info.descriptorSetLayout = set_layout;
	template_create_info.pipelineBindPoint = bind_point;
	template_create_info.pipelineLayout = pipeline_layout;

	VkDescriptorUpdateTemplate update_template = VK_NULL_HANDLE;
	VK_CHECK(vkCreateDescriptorUpdateTemplate(device, &template_create_info, nullptr, &update_template));

	return update_template;
}

VkPipeline CreateGraphicsPipeline(VkDevice device, VkPipelineCache pipeline_cache, VkRenderPass render_pass,
		VkPipelineLayout layout, VkShaderModule vert_or_mesh, VkShaderModule frag, bool rtx_enabled)
{
	assert(device);
	assert(vert_or_mesh);
	assert(frag);

	VkPipelineShaderStageCreateInfo stages[2] = {};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = rtx_enabled ? VK_SHADER_STAGE_MESH_BIT_NV : VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vert_or_mesh;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = frag;
	stages[1].pName = "main";

	VkPipelineVertexInputStateCreateInfo vertex_input = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

	VkPipelineInputAssemblyStateCreateInfo input_assembly = {
		VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO
	};
	input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineViewportStateCreateInfo viewport = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
	// Don't bake anything into the pipeline
	viewport.viewportCount = 1;
	// viewport.pViewports;
	viewport.scissorCount = 1;
	// viewport.pScissors;

	VkPipelineRasterizationStateCreateInfo raster_state = {
		VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO
	};
	raster_state.polygonMode = VK_POLYGON_MODE_FILL;
	// TODO: Count on 0 being ok for all this.
	raster_state.cullMode = VK_CULL_MODE_BACK_BIT;
	// raster_state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	// raster_state.depthBiasEnable;
	// raster_state.depthBiasConstantFactor;
	// raster_state.depthBiasClamp;
	// raster_state.depthBiasSlopeFactor;
	raster_state.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisample = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	// multisample.sampleShadingEnable;
	// multisample.minSampleShading;
	// multisample.pSampleMask;
	// multisample.alphaToCoverageEnable;
	// multisample.alphaToOneEnable;

	VkPipelineDepthStencilStateCreateInfo depth_stencil = {
		VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO
	};
	// depth_stencil.depthTestEnable;
	// depth_stencil.depthWriteEnable;
	// depth_stencil.depthCompareOp;
	// depth_stencil.depthBoundsTestEnable;
	// depth_stencil.stencilTestEnable;
	// depth_stencil.front;
	// depth_stencil.back;
	// depth_stencil.minDepthBounds;
	// depth_stencil.maxDepthBounds;

	VkPipelineColorBlendAttachmentState attachments[1] = {};
	attachments[0].colorWriteMask =
			VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo blend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
	// blend.logicOpEnable;
	// blend.logicOp;
	blend.attachmentCount = ARRAYSIZE(attachments);
	blend.pAttachments = attachments;
	// blend.blendConstants[4];

	VkDynamicState dynamic_states[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
	};

	VkPipelineDynamicStateCreateInfo dynamic = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	dynamic.dynamicStateCount = ARRAYSIZE(dynamic_states);
	dynamic.pDynamicStates = dynamic_states;

	VkGraphicsPipelineCreateInfo pipeline_create_info = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	pipeline_create_info.stageCount = ARRAYSIZE(stages);
	pipeline_create_info.pStages = stages;
	pipeline_create_info.pVertexInputState = &vertex_input;
	pipeline_create_info.pInputAssemblyState = &input_assembly;

	pipeline_create_info.pViewportState = &viewport;
	pipeline_create_info.pRasterizationState = &raster_state;
	pipeline_create_info.pMultisampleState = &multisample;
	pipeline_create_info.pDepthStencilState = &depth_stencil;
	pipeline_create_info.pColorBlendState = &blend;
	pipeline_create_info.pDynamicState = &dynamic;
	pipeline_create_info.layout = layout;
	pipeline_create_info.renderPass = render_pass;
	// pipeline_create_info.subpass = 0;

	VkPipeline pipeline = VK_NULL_HANDLE;
	VK_CHECK(vkCreateGraphicsPipelines(device, pipeline_cache, 1, &pipeline_create_info, nullptr, &pipeline));
	return pipeline;
}
