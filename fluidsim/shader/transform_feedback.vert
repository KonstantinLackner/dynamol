#version 450 core

in vec4 coords;
layout(rgba32f, binding = 0) uniform image3D image;
uniform vec3 minBounds;
uniform vec3 maxBounds;

out vec4 outCoords;

void main()
{
    /*if (clamp(coords.xyz, minBounds, maxBounds) != coords.xyz)
    {
        outCoords = coords;
    }
    else
    {
        outCoords = coords + vec4(
            imageLoad(image, ivec3(coords.xyz - minBounds)).xyz,
        0);
    }*/

    outCoords = coords + vec4(imageLoad(image, ivec3(coords.xyz - minBounds)).xyz, 0);
}