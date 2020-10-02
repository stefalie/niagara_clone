#version 450

struct Vertex
{
	float vx, vy, vz;
	float nx, ny, nz;
	float tu, tv;
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
	const vec3 normal = vec3(v.nx, v.ny, v.nz);
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
