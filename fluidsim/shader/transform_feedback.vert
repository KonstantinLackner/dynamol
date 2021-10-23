#version 450 core

in vec3 coords;
layout(binding = 0) uniform sampler3D sampler;
uniform vec3 minBounds;

out vec3 vCoords;

void main()
{
    vCoords = coords + textureLod(sampler, coords - minBounds, 0.0);
}