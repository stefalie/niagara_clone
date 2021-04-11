#version 450

#extension GL_GOOGLE_include_directive : require

#include "mesh.h"

layout(push_constant) uniform PushConstants
{
	MeshDraw mesh_draw;
};

layout(binding = 0) readonly buffer Vertices
{
	Vertex vertices[];
};

layout(location = 0) out vec4 color;

void main()
{
	const Vertex v = vertices[gl_VertexIndex];
	const vec3 position = vec3(v.vx, v.vy, v.vz);

#if USE_UNPACK
	// Both versions are equivalent for the way the normals are packed.
	const uint tmp = 0x80808080 ^ vertices[gl_VertexIndex].n_packed;
	const vec3 normal = unpackSnorm4x8(tmp).xyz;
	// const vec3 normal = unpackUnorm4x8(vertices[gl_VertexIndex].n_packed).xyz * 2.0 - vec3(1.0);
#else
	// Without arithmetic types, something like this is necessary.
	// const vec3 normal = vec3(int(vertices[gl_VertexIndex].nx), int(vertices[gl_VertexIndex].ny),
	// int(vertices[gl_VertexIndex].nz)) / 127.0 - vec3(1.0);
	const vec3 normal = vec3(v.nx, v.ny, v.nz) / 127.0 - vec3(1.0);
#endif

	const vec2 uv = vec2(v.tu, v.tv);

	//gl_Position = vec4(position * vec3(1, 1, 0.5) + vec3(0, 0, 0.5), 1.0);
	gl_Position = vec4(
			(position * vec3(mesh_draw.scale, 1.0) + vec3(mesh_draw.offset, 0.0)) * vec3(2, 2, 0.5) + vec3(-1, -1, 0.5),
			1.0);

	color = vec4(normal * 0.5 + vec3(0.5), 1.0);
}

// Mesh via vertex stream and explicit attribute definition in Vulkan
/*
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;

layout(location = 0) out vec4 color;

void main()
{
	gl_Position = vec4(position * vec3(1, 1, 0.5) + vec3(0, 0, 0.5), 1.0);

	color = vec4(normal * 0.5 + vec3(0.5), 1.0);
}
*/

// Single triangle
/*
const vec3[3] positions =
{
	vec3( 0.0,  0.5, 0.0),
	vec3( 0.5, -0.5, 0.0),
	vec3(-0.5, -0.5, 0.0),
};

void main()
{
	gl_Position = vec4(positions[gl_VertexIndex], 1.0);
}
*/
