#version 460
#extension GL_EXT_nonuniform_qualifier : enable

/*

	This shader generates the BRDF look-up table (BRDF integration map) used in image-based lighting for the specular light

*/

layout(location = 0) out vec2 tex_coord;

void main()
{
	tex_coord = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	gl_Position = vec4(tex_coord * 2.0 - 1.0, 0.0, 1.0);
}
