#version 450 core

in vec4 coords;
layout(binding = 0) uniform sampler3D sampler;
uniform vec3 minBounds;
uniform vec3 maxBounds;

uniform float forceMultiplier = 0.01;

out vec4 outCoords;

void main()
{
    /*if (clamp(coords.xyz, minBounds, maxBounds) != coords.xyz)
    {
        outCoords = coords;
    }
    else
    {*/
        outCoords = vec4(clamp(coords.xyz + textureLod(sampler, coords.xyz - minBounds, 0).xyz * forceMultiplier, minBounds, maxBounds), coords.w);
    //}
}