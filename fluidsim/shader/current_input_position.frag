#version 450 core

layout(location = 0) out vec4 fragColor;
uniform int colour = 0;

void main()
{
    if (colour == 0) {
        fragColor = vec4(1.0f, 0.0f, 0.0f, 1.0f);
    } else {
        fragColor = vec4(0.0f, 0.0f, 1.0f, 1.0f);
    }
}