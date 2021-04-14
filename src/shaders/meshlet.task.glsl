#version 450

#extension GL_NV_mesh_shader : require
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_shader_draw_parameters : require
#define BALLOT 1
#if BALLOT
#extension GL_KHR_shader_subgroup_ballot : require
#else
#extension GL_KHR_shader_subgroup_arithmetic : require
#endif

#include "mesh.h"

#define CULL 1

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

layout(binding = 0) readonly buffer Draws
{
	MeshDraw draws[];
};

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

bool ConeCull1(vec3 cone_axis, float cone_cutoff, vec3 view)
{
	// NOTE: Normally view points towards the camera.
	// Then if
	// angle(view, cone_axis) > cone_half_angle + 90 -> cull
	// dot(view, cone_axis) < cos(cone_half_angle + 90) -> cull
	// dot(view, cone_axis) < (-sin(cone_half_angle))
	// dot(view, cone_axis) < -sqrt(1 - cos(cone_half_angle) * cos(cone_half_angle))
	// dot(view, cone_axis) < -sqrt(1 - dp * dp)
	// dot(-view, cone_axis) > sqrt(1 - dp * dp)
	// dot(view_ray_dir, cone_axis) > sqrt(1 - dp * dp)
	// dot(view_ray_dir, cone_axis) > cone_cutoff
	//
	// Run with the following to see it "from the other side".
	// return dot(cone.xyz, view) > cone.w;
	return dot(cone_axis, -view) > cone_cutoff;  // From RH -> LH: remove -
}

bool ConeCull2(vec3 cone_apex, vec3 cone_axis, float cone_cutoff, vec3 camera_position)
{
	return dot(normalize(cone_apex - camera_position), cone_axis) >= cone_cutoff;
}

bool ConeCull3(vec3 center, float radius, vec3 cone_axis, float cone_cutoff, vec3 camera_position)
{
	// Compare with Figure 7 in:
	// Olsson, Billeter, Assarsson - 2012 - Clustered Deferred and Forward Shading
	//
	// view = normalize(camera_position - center)
	// angle(view, cone_axis) > cone_half_angle + 90 + view_cone_for_bounding_sphere_half_angle ->
	// dot(view, cone_axis) < cos(cone_half_angle + 90 + view_cone_for_bounding_sphere_half_angle)
	// dot(view, cone_axis) < -sin(cone_half_angle + view_cone_for_bounding_sphere_half_angle)
	// dot(view, cone_axis) < -(sin(cone_half_angle) * cos(view_cone_for_bounding_sphere_half_angle) +
	//                          cos(cone_half_angle) * sin(view_cone_for_bounding_sphere_half_angle))
	// NOTE: here I think mesh_optimizer took a small shortcut
	// dot(view, cone_axis) < -(sin(cone_half_angle) * 1 + 1 * sin(view_cone_for_bounding_sphere_half_angle))
	// dot(-view, cone_axis) > sin(cone_half_angle) + sin(view_cone_for_bounding_sphere_half_angle)
	// dot(-view, cone_axis) > sin(cone_half_angle) + radius / length(center - camera_position)
	// dot(-view, cone_axis) > cone_cutoff + radius / length(center - camera_position)
	return dot(center - camera_position, cone_axis) >=
			cone_cutoff * length(center - camera_position) + sqrt(1.0 - cone_cutoff * cone_cutoff) * radius;

	// NOTE: This still works btw (if you want to use it for real, precomute some of the terms):
	// return dot(center - camera_position, cone_axis) >= cone_cutoff * length(center - camera_position) + sqrt(1.0 -
	// cone_cutoff * cone_cutoff) * radius;
	// Or with both terms corrected:
	// const float sin_view_cone_half = radius / length(center - camera_position);
	// const float cos_view_cone_half = sqrt(1.0 - sin_view_cone_half * sin_view_cone_half);
	// return dot(center - camera_position, cone_axis) >=
	//		cone_cutoff * length(center - camera_position) + sqrt(1.0 - cone_cutoff * cone_cutoff) * radius;
}

void main()
{
	const uint gi = gl_WorkGroupID.x;
	const uint ti = gl_LocalInvocationID.x;
	const uint mi = gi * 32 + ti;
	const MeshDraw mesh_draw = draws[gl_DrawIDARB];

#if CULL
	// TODO: Assumes subgroup size 32.
#if BALLOT
	vec3 cone_axis = vec3(
			meshlets[mi].cone_axis[0] / 127.0, meshlets[mi].cone_axis[1] / 127.0, meshlets[mi].cone_axis[2] / 127.0);
	cone_axis = RotateVecByQuat(cone_axis, mesh_draw.orientation);
	// const bool accept1 = !ConeCull1(cone_axis, meshlets[mi].cone_cutoff, vec3(0, 0, 1));

	// const vec3 cone_apex =
	//		RotateVecByQuat(meshlets[mi].cone_apex, mesh_draw.orientation) * mesh_draw.scale + mesh_draw.position;
	// const bool accept2 = !ConeCull2(cone_apex, cone_axis, meshlets[mi].cone_cutoff, vec3(0));

	const vec3 center =
			RotateVecByQuat(meshlets[mi].center, mesh_draw.orientation) * mesh_draw.scale + mesh_draw.position;
	const float radius = meshlets[mi].radius * mesh_draw.scale;
	const float cone_cutoff = meshlets[mi].cone_cutoff / 127.0;
	const bool accept3 = !ConeCull3(center, radius, cone_axis, cone_cutoff, vec3(0));

	const bool accept = accept3;

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
