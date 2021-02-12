#version 450

#extension GL_NV_mesh_shader : require

layout(location = 0) in vec4 color;
layout(location = 1) perprimitiveNV in vec3 triangle_normal;

layout(location = 0) out vec4 out_color;


void main()
{
	//out_color = color;
	out_color = vec4(triangle_normal * 0.5 + vec3(0.5), 1.0);
}
