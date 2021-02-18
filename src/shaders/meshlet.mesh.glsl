#version 450

#extension GL_NV_mesh_shader : require
#extension GL_GOOGLE_include_directive : require

#include "mesh.h"

#define DEBUG 1

// 64 for potential AMD
layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
layout(triangles, max_vertices = 64, max_primitives = 124) out;

layout(binding = 0) readonly buffer Vertices
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
// General purpose -> 83 triangles
// Minecraft/Roblox is crazy (because you need different normals for the "same" vertex") -> 41 triangles

layout(binding = 1) readonly buffer Meshlets
{
	Meshlet meshlets[];
};

in taskNV task_block
{
	// uint meshlet_offset;
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
	// const uint mi = gl_WorkGroupID.x;
	// const uint mi = gl_WorkGroupID.x + meshlet_offset;
	const uint mi = meshlet_indices[gl_WorkGroupID.x];
	const uint ti = gl_LocalInvocationID.x;

	const uint vertex_count = meshlets[mi].vertex_count;
	const uint triangle_count = meshlets[mi].triangle_count;
	const uint index_count = 3 * triangle_count;

#if DEBUG
	const uint meshlet_hash = hash(mi);
	const vec3 meshlet_color =
			vec3(float(meshlet_hash & 255), float((meshlet_hash >> 8) & 255), float((meshlet_hash >> 16) & 255)) /
			255.0;
#endif

	for (uint i = ti; i < vertex_count; i += 32)
	{
		const uint vi = meshlets[mi].vertices[i];
		const Vertex v = vertices[vi];

		const vec3 position = vec3(v.vx, v.vy, v.vz);
#if USE_UNPACK
		const vec3 normal = unpackSnorm4x8(0x80808080 ^ v.n_packed).xyz;
#else
		const vec3 normal = vec3(v.nx, v.ny, v.nz) / 127.0 - vec3(1.0);
#endif
		const vec2 uv = vec2(v.tu, v.tv);

		gl_MeshVerticesNV[i].gl_Position = vec4(position * vec3(1, 1, 0.5) + vec3(0, 0, 0.5), 1.0);

		color[i] = vec4(normal * 0.5 + vec3(0.5), 1.0);
#if DEBUG
		color[i] = vec4(meshlet_color, 1.0);
#endif
	}

	//// Doesn't seem to work with the barrier and fetching from gl_MeshVerticesNV.
	////memoryBarrier();
	// for (uint i = ti; i < triangle_count; i += 32)
	//{
	//	const uint vi0 = meshlets[mi].vertices[uint(meshlets[mi].indices[3 * i + 0])];
	//	const uint vi1 = meshlets[mi].vertices[uint(meshlets[mi].indices[3 * i + 1])];
	//	const uint vi2 = meshlets[mi].vertices[uint(meshlets[mi].indices[3 * i + 2])];
	//
	//	const vec3 position0 = vec3(vertices[vi0].vx, vertices[vi0].vy, vertices[vi0].vz);
	//	const vec3 position1 = vec3(vertices[vi1].vx, vertices[vi1].vy, vertices[vi1].vz);
	//	const vec3 position2 = vec3(vertices[vi2].vx, vertices[vi2].vy, vertices[vi2].vz);
	//
	//	//const vec3 position0 = gl_MeshVerticesNV[vi0].gl_Position.xzy;
	//	//const vec3 position1 = gl_MeshVerticesNV[vi1].gl_Position.xzy;
	//	//const vec3 position2 = gl_MeshVerticesNV[vi2].gl_Position.xzy;
	//
	//	const vec3 normal = normalize(cross(position1 - position0, position2 - position0));
	//	triangle_normals[i] = normal;
	//}

#if !USE_PACKED_INDICES
	for (uint i = ti; i < index_count; i += 32)
	{
		gl_PrimitiveIndicesNV[i] = meshlets[mi].indices[i];
	}
#else
	const uint index_chunk_count = (index_count + 3) / 4;
	for (uint i = ti; i < index_chunk_count; i += 32)
	{
		writePackedPrimitiveIndices4x8NV(i * 4, meshlets[mi].indices[i]);
	}
	// const uint index_chunk_count = (index_count + 7) / 8;
	// for (uint i = ti; i < index_chunk_count; i += 32)
	//{
	//	writePackedPrimitiveIndices4x8NV(i * 8 + 0, meshlets[mi].indices[i * 2 + 0]);
	//	writePackedPrimitiveIndices4x8NV(i * 8 + 4, meshlets[mi].indices[i * 2 + 1]);
	//}
#endif

	if (ti == 0)
	{
		gl_PrimitiveCountNV = triangle_count;
	}
}
