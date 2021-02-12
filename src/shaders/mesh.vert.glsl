#version 450

#define USE_UNPACK 0  // Pack is faster it seems
// I guess intead of relying on these extensions we could also make use of
// the pack/unpack intrinsics. Done!
#if !USE_UNPACK
#extension GL_EXT_shader_8bit_storage: require
//#extension GL_EXT_shader_explicit_arithmetic_types_int8: require
#endif

#extension GL_EXT_shader_16bit_storage: require
//#extension GL_EXT_shader_explicit_arithmetic_types_float16: require

// Enables all arithmetic types.
#extension GL_EXT_shader_explicit_arithmetic_types: require

struct Vertex
{
	//float vx, vy, vz;
	float16_t vx, vy, vz;
	//float nx, ny, nz;
	// TODO: unpack currently doesn't work, because the alignment of the field
	// inside the Vertex struct is incorrect with the uint.
#if USE_UNPACK
	uint n_packed;
#else
	uint8_t nx, ny, nz, nw;
#endif
	//float tu, tv;
	float16_t tu, tv;
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
	//const vec3 normal = unpackUnorm4x8(vertices[gl_VertexIndex].n_packed).xyz * 2.0 - vec3(1.0);
#else
	// Without arithmetic types, something like this is necessary.
	// const vec3 normal = vec3(int(vertices[gl_VertexIndex].nx), int(vertices[gl_VertexIndex].ny), int(vertices[gl_VertexIndex].nz)) / 127.0 - vec3(1.0);
	const vec3 normal = vec3(v.nx, v.ny, v.nz) / 127.0 - vec3(1.0);
#endif

	const vec2 uv = vec2(v.tu, v.tv);

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
