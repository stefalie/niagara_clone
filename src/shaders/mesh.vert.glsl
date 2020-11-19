#version 450

#define USE_UNPACK 1  // Pack is faster it seems
#define USE_EXPLICIT_ARITHMETIC 1

#if !USE_UNPACK
// I guess intead of relying on these extensions we could also make use of
// the pack/unpack intrinsics. Done!
#extension GL_EXT_shader_16bit_storage: require
#extension GL_EXT_shader_8bit_storage: require
#if USE_EXPLICIT_ARITHMETIC
#extension GL_EXT_shader_explicit_arithmetic_types: require
//#extension GL_EXT_shader_explicit_arithmetic_types_int8: require
#endif
#endif


struct Vertex
{
	float vx, vy, vz;
	//float nx, ny, nz;
#if USE_UNPACK
	uint n_packed;
#else
	uint8_t nx, ny, nz, nw;
#endif
	float tu, tv;
};

layout(binding = 0) readonly buffer Vertices
{
	Vertex vertices[];
};

layout(location = 0) out vec4 color;

void main()
{
#if (USE_UNPACK || USE_EXPLICIT_ARITHMETIC)
	const Vertex v = vertices[gl_VertexIndex];
	const vec3 position = vec3(v.vx, v.vy, v.vz);
#if USE_UNPACK
	// Both versions are equivalent for the way the normals are packed.
	const uint tmp = 0x80808080 ^ v.n_packed;
	const vec3 normal = unpackSnorm4x8(tmp).xyz;
	//const vec3 normal = unpackUnorm4x8(v.n_packed).xyz * 2.0 - vec3(1.0);
#else
	const vec3 normal = vec3(v.nx, v.ny, v.nz) / 127.0 - vec3(1.0);
#endif
	const vec2 uv = vec2(v.tu, v.tv);

#else
	const vec3 position = vec3(vertices[gl_VertexIndex].vx, vertices[gl_VertexIndex].vy, vertices[gl_VertexIndex].vz);
	const vec3 normal = vec3(int(vertices[gl_VertexIndex].nx), int(vertices[gl_VertexIndex].ny), int(vertices[gl_VertexIndex].nz)) / 127.0 - vec3(1.0);
	const vec2 uv = vec2(vertices[gl_VertexIndex].tu, vertices[gl_VertexIndex].tv);
#endif

	gl_Position = vec4(position * vec3(1, 1, 0.5) + vec3(0, 0, 0.5), 1.0);

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
