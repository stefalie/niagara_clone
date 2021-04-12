#define USE_UNPACK 1  // Pack/unpack enabled seems to be faster
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
	// float16_t vx, vy, vz, vw;
	// float nx, ny, nz;
#if USE_UNPACK
	uint n_packed;
#else
	uint8_t nx, ny, nz, nw;  // Could be encoded in just nx, ny.
#endif
	// float tu, tv;
	float16_t tu, tv;
};

struct Meshlet
{
	vec4 cone;  // Meshlet struct needs to be 16 byte aligned.
	uint data_offset;
	uint8_t vertex_count;
	uint8_t triangle_count;
};


struct MeshDraw
{
	mat4 projection;
	vec3 position;
	float scale;
	vec4 orientation;
};

vec3 rotateVecByQuat(vec3 v, vec4 q)
{
	return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}
