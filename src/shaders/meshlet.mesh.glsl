#version 450

//#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_8bit_storage : require
#extension GL_NV_mesh_shader : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8: require

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
layout(triangles, max_vertices = 64, max_primitives = 42) out;

struct Vertex
{
	float vx, vy, vz;
	uint8_t nx, ny, nz, nw;
	float tu, tv;
};

layout(binding = 0) readonly buffer Vertices
{
	Vertex vertices[];
};

// N triangles
// 3 * N vertices (when duplicating everything)
// N / 2 unique vertices in an ideal world (valence 6)
// [N / 2, N] unique vertices in practice for high quality meshes.
// [24, 42] triangles -> Let's use local_size_x == 32
struct Meshlet
{
	uint vertices[64];
	uint8_t indices[126];  // Max 42 triangles.
	uint8_t vertex_count;
	uint8_t index_count;
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

		const vec3 position = vec3(vertices[vi].vx, vertices[vi].vy, vertices[vi].vz);
		const vec3 normal = vec3(vertices[vi].nx, vertices[vi].ny, vertices[vi].nz) / 127.0 - vec3(1.0);
		const vec2 uv = vec2(vertices[vi].tu, vertices[vi].tv);

		gl_MeshVerticesNV[i].gl_Position = vec4(position * vec3(1, 1, 0.5) + vec3(0, 0, 0.5), 1.0);

		color[i] = vec4(normal * 0.5 + vec3(0.5), 1.0);
	}

	gl_PrimitiveCountNV = meshlets[mi].index_count / 3;

	for (int i = 0; i < meshlets[mi].index_count; ++i)
	{
		gl_PrimitiveIndicesNV[i] = meshlets[mi].indices[i];
	}
}
