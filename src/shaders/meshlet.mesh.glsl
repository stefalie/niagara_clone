#version 450

#extension GL_NV_mesh_shader : require

#define USE_UNPACK 0
#if USE_UNPACK
#extension GL_EXT_shader_8bit_storage : require
#endif

#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types: require

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
layout(triangles, max_vertices = 64, max_primitives = 42) out;

struct Vertex
{
	float16_t vx, vy, vz;
#if USE_UNPACK
	uint n_packed;
#else
	uint8_t nx, ny, nz, nw;
#endif
	float16_t tu, tv;
};

layout(binding = 0) readonly buffer Vertices
{
	Vertex vertices[];
};

// N triangles
// 3 * N vertices (when duplicating everything)
// N / 2 unique vertices in an ideal world (valence 6)
// [N / 2, N] unique vertices in practice for high quality meshes.
// [24, 42] vertices -> Let's use local_size_x == 32
struct Meshlet
{
	uint vertices[64];
	uint8_t indices[126];  // Max 42 triangles.
	uint8_t vertex_count;
	uint8_t triangle_count;
};

layout(binding = 1) readonly buffer Meshlets
{
	Meshlet meshlets[];
};

layout(location = 0) out vec4 color[];

void main()
{
	const uint mi = gl_WorkGroupID.x;

	// TODO ;-)
	for (int i = 0; i < meshlets[mi].vertex_count; ++i)
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
	}

	gl_PrimitiveCountNV = meshlets[mi].triangle_count;

	const int index_count = 3 * meshlets[mi].triangle_count;
	for (int i = 0; i < index_count; ++i)
	{
		gl_PrimitiveIndicesNV[i] = meshlets[mi].indices[i];
	}
}
