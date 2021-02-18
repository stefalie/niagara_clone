#define USE_UNPACK 0  // Pack is faster it seems
// I guess intead of relying on these extensions we could also make use of
// the pack/unpack intrinsics. Done!
#if !USE_UNPACK
#extension GL_EXT_shader_8bit_storage : require
// #extension GL_EXT_shader_explicit_arithmetic_types_int8: require
#endif

#extension GL_EXT_shader_16bit_storage : require
// #extension GL_EXT_shader_explicit_arithmetic_types_float16: require

// Enables all arithmetic types.
#extension GL_EXT_shader_explicit_arithmetic_types : require


struct Vertex
{
	float vx, vy, vz;
	//float16_t vx, vy, vz, vw;
	// float nx, ny, nz;
	// TODO: unpack currently doesn't work, because the alignment of the field
	// inside the Vertex struct is incorrect with the uint.
#if USE_UNPACK
	uint n_packed;
#else
	uint8_t nx, ny, nz, nw;     // Could be encoded in just nx, ny.
#endif
	// float tu, tv;
	float16_t tu, tv;
};

#define USE_PACKED_INDICES 1

struct Meshlet
{
	vec4 cone;  // No vec4, because that woul need 16-byte alignment
	uint vertices[64];
#if !USE_PACKED_INDICES
	uint8_t indices[124 * 3];  // Max 126 triangles. 124 for by-4-divisibility
							   // uint8_t pad_1;
							   // uint8_t pad_2;
#else                          // This will completely break the normal computation.
	uint indices[124 * 3 / 4];  // Max 126 triangles.
#endif
	uint8_t vertex_count;
	uint8_t triangle_count;
};
