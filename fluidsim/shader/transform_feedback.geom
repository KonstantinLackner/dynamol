#version 450 core

layout(points) in;
layout(points) out;
layout(max_vertices = 1) out;

in vec4[] vCoords;
out vec4 gCoords;

void main()
{
    gCoords = vCoords[0];
    EmitVertex();
    EndPrimitive();
}