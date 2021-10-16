#version 420 core

out vec4 FragColor;

/*layout(rgba16_snorm) 
uniform image3D field;*/

uniform sampler3D sampler;

uniform float depth;
//uniform ivec3 cubeSize;

in vec2 vTex;

void main()
{
   //vec4 color = imageLoad(field, ivec3(vec3(vTex, depth)) * cubeSize);

   vec4 color = texture(sampler, vec3(vTex, depth));
   FragColor = color / 2 + 0.5;
};
