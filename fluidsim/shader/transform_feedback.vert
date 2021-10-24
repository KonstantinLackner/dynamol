#version 450 core

in vec4 coords;
layout(rgba16_snorm, binding = 0) uniform image3D image;
uniform vec3 minBounds;

out vec4 vCoords;

void main()
{
    vCoords = coords + vec4(imageLoad(image, ivec3(coords.xyz - minBounds)).xyz, 0);
}