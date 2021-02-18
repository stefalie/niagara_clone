#version 450

#extension GL_NV_mesh_shader : require
#extension GL_GOOGLE_include_directive : require
#define BALLOT 1
#if BALLOT
#extension GL_KHR_shader_subgroup_ballot : require
#else
#extension GL_KHR_shader_subgroup_arithmetic : require
#endif

#include "mesh.h"

#define CULL 1

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

layout(binding = 1) readonly buffer Meshlets
{
	Meshlet meshlets[];
};

// Causes: https://github.com/KhronosGroup/Vulkan-ValidationLayers/issues/2102
out taskNV task_block
{
	// uint meshlet_offset;
	uint meshlet_indices[32];
};

bool coneCull(vec4 cone, vec3 view)
{
	// NOTE: Normally view points towards the camera.
	// Then if
	// dot(view, cone) < cos(cone_half + 90) -> cull
	// dot(view, cone) < (-sin(cone_half))
	// dot(view, cone) < -sqrt(1 - cos(cone_half) * cos(cone_half))
	// dot(view, cone) < -sqrt(1 - dp * dp)
	// dot(-view, cone) > sqrt(1 - dp * dp)
	// dot(view_ray_dir, cone) > sqrt(1 - dp * dp)
	// dot(view_ray_dir, cone) > cone.w
	//
	// Run with the following to see it "from the other side".
	// return dot(cone.xyz, view) > cone.w;
	return dot(cone.xyz, -view) > cone.w;
}

// shared uint meshlet_count;

void main()
{
	const uint gi = gl_WorkGroupID.x;
	const uint ti = gl_LocalInvocationID.x;
	const uint mi = gi * 32 + ti;

#if CULL
	// TODO: Assumes subgroup size 32.
#if BALLOT
	const bool accept = !coneCull(meshlets[mi].cone, vec3(0, 0, 1));
	const uvec4 ballot = subgroupBallot(accept);
	const uint index = subgroupBallotExclusiveBitCount(ballot);

	if (accept)
	{
		meshlet_indices[index] = mi;
	}
	if (subgroupElect())
	{
		gl_TaskCountNV = subgroupBallotBitCount(ballot);
	}
#else
	const uint accept = coneCull(meshlets[mi].cone, vec3(0, 0, 1)) ? 0 : 1;
	const uint index = subgroupExclusiveAdd(accept);

	if (accept == 1)
	{
		meshlet_indices[index] = mi;
	}
	if (ti == 31)
	{
		gl_TaskCountNV = index + accept;
	}
#endif
#else
	meshlet_indices[ti] = mi;
	if (ti == 0)
	{
		// meshlet_offset = mi * 32;
		gl_TaskCountNV = 32;
	}
#endif
}
