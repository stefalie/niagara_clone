#version 450

#extension GL_NV_mesh_shader : require
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_shader_draw_parameters : require

#include "mesh.h"

#define DEBUG 0

// 64 for potential AMD
layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
layout(triangles, max_vertices = 64, max_primitives = 124) out;

layout(push_constant) uniform PushConstants
{
	Globals globals;
};

layout(binding = 0) readonly buffer Draws
{
	MeshDraw draws[];
};

layout(binding = 3) readonly buffer Vertices
{
	Vertex vertices[];
};

// N triangles
// 3 * N vertices (when duplicating everything)
// N / 2 unique vertices in an ideal world (valence 6)
// [N / 2, N] unique vertices in practice for high quality meshes.
// OLD: // [24, 42] vertices -> Let's use local_size_x == 32

// 126 triangles only makes sense for quite optimal meshes close to N / 2 vertices.
// If not, you'll always fill up the 64 vertices without even getting anywhere
// close to the 126 triangles. If that's the case go down to 84 triangles,
// that might be more realistic.
// Note that Arseny, so far, didn't give an explanation of why we're fixed on 64
// vertices. I mean it's nice as it's exactly 2 warps, and it will
// stay at about 1/8th of the potential 16 kB output.
// 16 kB allows for 256 b if using 64 vertices, we only use 32 b (position + color).
//
// Terrain is probably close to optimal -> 126 triangles
// General purpose -> 83 triangles (84 no? (2 * 128 - 4) / 3)
// Minecraft/Roblox is crazy (because you need different normals for the "same" vertex") -> 41 triangles

layout(binding = 1) readonly buffer Meshlets
{
	Meshlet meshlets[];
};

#define USE_PACKED_INDICES 1

layout(binding = 2) readonly buffer MeshletData
{
	// Imagine the layout as:
	// struct MeshletData
	// {
	//     uint vertices[MAX 64];
	// #if !USE_PACKED_INDICES
	//     uint8_t indices[(MAX 124 * 3)];  // Max 126 triangles. 124 for by-4-divisibility
	// #else
	//     uint indices_packed[MAX (124 * 3 / 4)];  // Max 126 triangles.
	// #endif
	// } meshlet_data[N];
	uint meshlet_data[];
};

in taskNV task_block
{
	uint meshlet_indices[32];
};

layout(location = 0) out vec4 color[];

// layout(location = 1) perprimitiveNV out vec3 triangle_normals[];

uint hash(uint a)
{
	a = (a + 0x7ed55d16) + (a << 12);
	a = (a ^ 0xc761c23c) ^ (a >> 19);
	a = (a + 0x165667b1) + (a << 5);
	a = (a + 0xd3a2646c) ^ (a << 9);
	a = (a + 0xfd7046c5) + (a << 3);
	a = (a ^ 0xb55a4f09) ^ (a >> 16);
	return a;
}

void main()
{
	const uint mi = meshlet_indices[gl_WorkGroupID.x];
	const uint ti = gl_LocalInvocationID.x;

	const uint vertex_count = meshlets[mi].vertex_count;
	const uint triangle_count = meshlets[mi].triangle_count;
	const uint index_count = 3 * triangle_count;

	const uint data_offset = meshlets[mi].data_offset;
	const uint vertex_offset = data_offset;
	const uint index_offset = data_offset + vertex_count;

	const MeshDraw mesh_draw = draws[gl_DrawIDARB];

#if DEBUG
	const uint meshlet_hash = hash(mi);
	const vec3 meshlet_color =
			vec3(float(meshlet_hash & 255), float((meshlet_hash >> 8) & 255), float((meshlet_hash >> 16) & 255)) /
			255.0;
#endif

	for (uint i = ti; i < vertex_count; i += 32)
	{
		const uint vi = meshlet_data[vertex_offset + i];
		const Vertex v = vertices[vi];

		const vec3 position = vec3(v.vx, v.vy, v.vz);
#if USE_UNPACK
		const vec3 normal = unpackSnorm4x8(0x80808080 ^ v.n_packed).xyz;
#else
		const vec3 normal = vec3(v.nx, v.ny, v.nz) / 127.0 - vec3(1.0);
#endif
		const vec2 uv = vec2(v.tu, v.tv);

		gl_MeshVerticesNV[i].gl_Position = globals.projection *
				vec4(RotateVecByQuat(position, mesh_draw.orientation) * mesh_draw.scale + mesh_draw.position, 1.0);

		color[i] = vec4(normal * 0.5 + vec3(0.5), 1.0);
#if DEBUG
		color[i] = vec4(meshlet_color, 1.0);
#endif
	}

#if !USE_PACKED_INDICES
	for (uint i = ti; i < index_count; i += 32)
	{
		gl_PrimitiveIndicesNV[i] = (meshlet_data[index_offset + i / 4] >> ((i % 4) * 8)) & 255u;
	}
#else
	const uint index_chunk_count = (index_count + 3) / 4;
	for (uint i = ti; i < index_chunk_count; i += 32)
	{
		writePackedPrimitiveIndices4x8NV(i * 4, meshlet_data[index_offset + i]);
	}
#endif

	if (ti == 0)
	{
		gl_PrimitiveCountNV = triangle_count;
	}
}
