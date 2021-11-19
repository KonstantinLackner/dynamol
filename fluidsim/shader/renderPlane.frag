#version 420 core

out vec4 FragColor;

/*layout(rgba16_snorm) 
uniform image3D field;*/

layout(binding = 0) uniform sampler3D sampler;

uniform float depth;
uniform vec2 range;
//uniform ivec3 cubeSize;

in vec2 vTex;

void main()
{
   vec3 color = texture(sampler, vec3(vTex, depth)).rgb;
   vec3 mapped = (color - range.x) / (range.y - range.x);

   FragColor = vec4(mapped, 1.0);
};
