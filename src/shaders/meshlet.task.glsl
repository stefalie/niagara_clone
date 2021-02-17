#version 450

#extension GL_NV_mesh_shader : require
#extension GL_GOOGLE_include_directive : require

#include "mesh.h"

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

layout(binding = 1) readonly buffer Meshlets
{
	Meshlet meshlets[];
};

out taskNV task_block
{
	uint meshlet_offset;
};

void main()
{
	const uint mi = gl_WorkGroupID.x;
	const uint ti = gl_LocalInvocationID.x;

	if (ti == 0)
	{
		meshlet_offset = mi * 32;
		gl_TaskCountNV = 32;
	}
}
