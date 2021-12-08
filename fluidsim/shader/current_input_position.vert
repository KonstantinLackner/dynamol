#version 450 core

uniform mat4 modelViewProjectionMatrix;

layout(location = 0) in vec3 coords;

void main()
{
    gl_Position = modelViewProjectionMatrix * vec4(coords, 1.0);
}