const GLchar* vertexShaderSrc = R"glsl(
    in vec3 inValue;
    uniform sampler2D grid;
    out vec3 outValue;

    void main()
    {
        // find valueinGrid
        outValue = inValue + valueinGrid; 
    }
)glsl";