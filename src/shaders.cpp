#include "common.h"

#include "shaders.h"

#include <stdio.h>

#include <vector>

#include <spirv-headers/spirv.h>

// https://www.khronos.org/registry/spir-v/specs/1.0/SPIRV.pdf

static VkShaderStageFlagBits GetShaderStage(SpvExecutionModel model)
{
	switch (model)
	{
	case SpvExecutionModelVertex:
		return VK_SHADER_STAGE_VERTEX_BIT;
	case SpvExecutionModelFragment:
		return VK_SHADER_STAGE_FRAGMENT_BIT;
	case SpvExecutionModelTaskNV:
		return VK_SHADER_STAGE_TASK_BIT_NV;
	case SpvExecutionModelMeshNV:
		return VK_SHADER_STAGE_MESH_BIT_NV;
	default:
		assert(!"Unsupported shader execution model!");
		return VkShaderStageFlagBits(0);
	}
}

struct Id
{
	enum Kind
	{
		Unknown,
		Variable
	};
	Kind kind = Unknown;
	uint32_t type;
	uint32_t storage_class;
	uint32_t binding;
	uint32_t set;
};

static void ParseShader(Shader& shader, const uint32_t* code, uint32_t code_size)
{
	assert(code[0] == SpvMagicNumber);
	const uint32_t id_bound = code[3];

	std::vector<Id> ids(id_bound);

	const uint32_t* inst = code + 5;
	while (inst != code + code_size)
	{
		const uint16_t opcode = uint16_t(inst[0]);
		const uint16_t word_count = uint16_t((inst[0]) >> 16);

		switch (opcode)
		{
		case SpvOpEntryPoint: {
			assert(word_count >= 2);
			shader.stage = GetShaderStage(SpvExecutionModel(inst[1]));
			break;
		}
		case SpvOpDecorate: {
			assert(word_count >= 3);
			const uint32_t id = inst[1];
			assert(id < id_bound);

			switch (inst[2])
			{
			case SpvDecorationDescriptorSet:
				ids[id].set = inst[3];
				break;
			case SpvDecorationBinding:
				ids[id].binding = inst[3];
				break;
			}
			break;
		}
		case SpvOpVariable: {
			assert(word_count >= 4);
			const uint32_t id = inst[2];
			assert(id < id_bound);

			assert(ids[id].kind == Id::Unknown);
			ids[id].kind = Id::Variable;
			ids[id].type = inst[1];
			ids[id].storage_class = inst[3];
			break;
		}
		}

		assert(inst + word_count <= code + code_size);
		inst += word_count;
	}

	for (auto& id : ids)
	{
		if (id.kind == Id::Variable && id.storage_class == SpvStorageClassUniform)
		{
			// TODO: Assume we only have storage buffers.
			assert(id.set == 0);
			assert(id.binding < 32);
			assert((shader.storage_buffer_mask & (1 << id.binding)) == 0);

			shader.storage_buffer_mask |= 1 << id.binding;
		}
	}
}

bool LoadShader(Shader& shader, VkDevice device, const char* path)
{
	assert(device);

	FILE* file = fopen(path, "rb");
	if (!file)
	{
		return false;
	}
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
	shader_create_info.codeSize = length;  // Number of bytes, not words.
	shader_create_info.pCode = (const uint32_t*)buffer;

	VkShaderModule shader_module = VK_NULL_HANDLE;
	// TODO: More error handling.
	VK_CHECK(vkCreateShaderModule(device, &shader_create_info, nullptr, &shader_module));

	ParseShader(shader, (const uint32_t*)buffer, length / 4);

	free(buffer);
	shader.module = shader_module;
	// shader.stage = ??;

	return true;
}

void DestroyShader(Shader& shader, VkDevice device)
{
	vkDestroyShaderModule(device, shader.module, nullptr);
}

VkDescriptorSetLayout CreateDescriptorSetLayout(VkDevice device, Shaders shaders)
{
	std::vector<VkDescriptorSetLayoutBinding> set_layout_bindings;

	uint32_t storagebuffer_mask = 0;
	for (const Shader* shader : shaders)
	{
		storagebuffer_mask |= shader->storage_buffer_mask;
	}

	for (uint32_t i = 0; i < 32; ++i)
	{
		if (storagebuffer_mask & (1 << i))
		{
			VkDescriptorSetLayoutBinding binding = {};
			binding.binding = i;
			binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			binding.descriptorCount = 1;
			for (const Shader* shader : shaders)
			{
				if (shader->storage_buffer_mask & (1 << i))
				{
					binding.stageFlags |= shader->stage;
				}
			}

			set_layout_bindings.push_back(binding);
		}
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
		VkDescriptorSetLayout set_layout, VkPipelineLayout pipeline_layout, Shaders shaders)
{
	assert(device);

	std::vector<VkDescriptorUpdateTemplateEntry> entries;

	uint32_t storagebuffer_mask = 0;
	for (const Shader* shader : shaders)
	{
		storagebuffer_mask |= shader->storage_buffer_mask;
	}

	for (uint32_t i = 0; i < 32; ++i)
	{
		if (storagebuffer_mask & (1 << i))
		{
			VkDescriptorUpdateTemplateEntry entry = {};
			entry.dstBinding = i;
			entry.dstArrayElement = 0;
			entry.descriptorCount = 1;
			entry.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			entry.offset = sizeof(DescriptorInfo) * i;  // Hmm? TODO: I'd rather have multiplied this by entries.size().
			entry.stride = sizeof(DescriptorInfo);
			entries.push_back(entry);
		}
	}

	VkDescriptorUpdateTemplateCreateInfo template_create_info = {
		VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO
	};
	template_create_info.flags;
	template_create_info.descriptorUpdateEntryCount = uint32_t(entries.size());
	template_create_info.pDescriptorUpdateEntries = entries.data();
	template_create_info.templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR;
	// Note needed with push descriptor.
	// template_create_info.descriptorSetLayout = set_layout;
	template_create_info.pipelineBindPoint = bind_point;
	template_create_info.pipelineLayout = pipeline_layout;

	VkDescriptorUpdateTemplate update_template = VK_NULL_HANDLE;
	VK_CHECK(vkCreateDescriptorUpdateTemplate(device, &template_create_info, nullptr, &update_template));

	return update_template;
}

VkPipeline CreateGraphicsPipeline(VkDevice device, VkPipelineCache pipeline_cache, VkRenderPass render_pass,
		VkPipelineLayout layout, Shaders shaders)
{
	assert(device);

	std::vector<VkPipelineShaderStageCreateInfo> stages;
	for (const Shader* shader : shaders)
	{
		assert(shader->module);
		VkPipelineShaderStageCreateInfo stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
		stage.stage = shader->stage;
		stage.module = shader->module;
		stage.pName = "main";
		stages.push_back(stage);
	}

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
	raster_state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	// raster_state.frontFace = VK_FRONT_FACE_CLOCKWISE;
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
	// depth_stencil.depthTestEnable = VK_TRUE;
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
	pipeline_create_info.stageCount = uint32_t(stages.size());
	pipeline_create_info.pStages = stages.data();
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
