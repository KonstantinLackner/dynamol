#include "MainProgram.h"
#include "Shader.h" 

#include <fstream>
#include <regex>
#include <stdexcept>
#include <sstream>
#include <string>
#include <string_view>

#define NOMINMAX
#include <Windows.h>

static void APIENTRY DebugMessageCallback(const GLenum source, const GLenum type, const GLuint id, const GLenum severity, const GLsizei length, const GLchar* const message, const void* const userParam)
{
    std::ostringstream msg;
    msg << "source: " << source << ", type: " << type << ", id: " << id << ", severity: " << severity << ", message: " << std::string{ reinterpret_cast<const char* const>(message), static_cast<std::size_t>(length) } << "\n";
    OutputDebugStringA(msg.str().c_str());
}

MainProgram::MainProgram(GLFWwindow *const window, std::int32_t windowWidth, std::int32_t windowHeight, std::array<std::int32_t, 3> cubeDimensions)
    : window{window},
      windowWidth{windowWidth},
      windowHeight{windowHeight},
      cubeDimensions{cubeDimensions},
      velocityTexture{cubeDimensions[0], cubeDimensions[1], cubeDimensions[2], 4, true},
      pressureTexture{cubeDimensions[0], cubeDimensions[1], cubeDimensions[2], 4, true},
      divergenceTexture{cubeDimensions[0], cubeDimensions[1], cubeDimensions[2], 1, true},
	  temporaryTexture{cubeDimensions[0], cubeDimensions[1], cubeDimensions[2], 4, true},
      gridScale{1.0f / cubeDimensions[0]},
      splatRadius{cubeDimensions[0] * 0.37f},
      dt{0},
      lastTime{0},
      frameCounter{0},
      frameTimeSum{0}
{
    glGenQueries(1, &timerQuery);

    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(&DebugMessageCallback, nullptr);

    glEnable(GL_DEPTH_TEST);
    glLineWidth(1.0f);
    glEnable(GL_LINE_SMOOTH);
    glViewport(0, 0, windowWidth, windowHeight);
    glClearColor(0.0, 0.0, 0.0, 1.0);
}

MainProgram::~MainProgram()
{
    glDeleteQueries(1, &timerQuery);
}

void MainProgram::Run()
{
    while (!glfwWindowShouldClose(window))
    {
        double now = glfwGetTime();
        dt = lastTime == 0 ? 0.016667 : now - lastTime;
        lastTime = now;

        glfwPollEvents();
        Execute();
    }
}

namespace Variables
{
    static constexpr float Dissipation{0.98f};
    static constexpr float Gravity{8.0f};
    static constexpr float Viscosity{0.0004};
    static constexpr bool Boundaries{false};
    static constexpr std::size_t NumJacobiRounds{4};
    static constexpr float ForceMultiplier{1.0f};
}

void MainProgram::Execute()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    DoDroplets();

    using namespace Variables;

    glBeginQuery(GL_TIME_ELAPSED, timerQuery);

#pragma region Advection
    advectionShaderProgram.Select();
    advectionShaderProgram.SetUniform("delta_t", dt);
    advectionShaderProgram.SetUniform("dissipation", Dissipation);
    advectionShaderProgram.SetUniform("gs", gridScale);
    advectionShaderProgram.SetUniform("gravity", Gravity);
    BindImage("quantity_r", velocityTexture.GetFront(), 0, GL_READ_ONLY);
    BindImage("quantity_w", velocityTexture.GetBack(), 0, GL_WRITE_ONLY);
    BindImage("velocity", velocityTexture.GetFront(), 0, GL_READ_ONLY);
    Compute();
    velocityTexture.SwapBuffers();
#pragma endregion

#pragma region Impulse
    if (impulseState.ForceActive)
    {
        addImpulseShaderProgram.Select();
        addImpulseShaderProgram.SetUniform("position", impulseState.CurrentPos);
        addImpulseShaderProgram.SetUniform("radius", splatRadius);
        addImpulseShaderProgram.SetUniform("force", glm::vec4{impulseState.Delta, 0});
        BindImage("field_r", velocityTexture.GetFront(), 0, GL_READ_ONLY);
        BindImage("field_w", velocityTexture.GetBack(), 1, GL_WRITE_ONLY);
        Compute();
        velocityTexture.SwapBuffers();
        impulseState.ForceActive = false;
    }

#pragma endregion

#pragma region Diffuse
    const float alpha{(gridScale * gridScale) / (Viscosity * dt)};
    const float beta{alpha + 6.0f};
    SolvePoissonSystem(velocityTexture, velocityTexture.GetFront(), alpha, beta);
#pragma endregion

#pragma region Projection
    divergenceShaderProgram.Select();
    divergenceShaderProgram.SetUniform("gs", gridScale);
    BindImage("field_r", velocityTexture.GetFront(), 0, GL_READ_ONLY);
    BindImage("field_w", velocityTexture.GetBack(), 1, GL_WRITE_ONLY);
    Compute();

    SolvePoissonSystem(pressureTexture, velocityTexture.GetBack(), -1, 6.0f);

    gradientShaderProgram.Select();
    gradientShaderProgram.SetUniform("gs", gridScale);
    BindImage("field_r", pressureTexture.GetFront(), 0, GL_READ_ONLY);
    BindImage("field_w", pressureTexture.GetBack(), 1, GL_WRITE_ONLY);
    Compute();

    subtractShaderProgram.Select();
    BindImage("a", velocityTexture.GetFront(), 0, GL_READ_ONLY);
    BindImage("b", pressureTexture.GetFront(), 1, GL_READ_ONLY);
    BindImage("c", velocityTexture.GetBack(), 2, GL_WRITE_ONLY);
    Compute();
    velocityTexture.SwapBuffers();
#pragma endregion

#pragma region Bounds
    if (Boundaries)
    {
        SetBounds(velocityTexture, -1);
    }
#pragma endregion
    // Transform feedback read

    glEndQuery(GL_TIME_ELAPSED);

    GLuint elapsedTime;
    glGetQueryObjectuiv(timerQuery, GL_QUERY_RESULT, &elapsedTime);

    frameTimeSum += elapsedTime / 1000;

    if (++frameCounter == 100)
    {
        std::cout << frameTimeSum / static_cast<float>(frameCounter) << '\n';
        frameCounter = 0;
        frameTimeSum = 0;
    }

#pragma region Render
    renderPlaneShaderProgram.Select();
    velocityTexture.Bind(0);
    renderPlaneShaderProgram.SetUniform("sampler", glUniform1i, 0);
    //BindImage("field", velocityTexture.GetFront(), 0, GL_READ_ONLY);
    renderPlaneShaderProgram.SetUniform("depth", 0.0f);
    //renderPlaneShaderProgram.SetUniform("cubeSize", glUniform3iv, 1, cubeDimensions.data());
    quad.Bind();

    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    quad.Draw();
#pragma endregion

    glfwSwapBuffers(window);
}

static std::string LoadShader(std::string_view name)
{
    std::ifstream file;
    file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    file.open(name.data());

    return { std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{} };
};

void MainProgram::LoadShaders()
{
    CStdGLShader texCoordShader{CStdShader::Type::Vertex, LoadShader("Shaders/tex_coords.vert")};
    // Redefining old keywords
    texCoordShader.SetMacro("varying", "out");
    texCoordShader.SetMacro("attribute", "in");
    texCoordShader.Compile();

    const auto addShaderProgram = [&texCoordShader](CStdGLShaderProgram& program, std::string_view name)
    {
        CStdGLShader fragmentShader{ CStdShader::Type::Fragment, LoadShader(std::string{"Shaders/"} + name.data() + ".frag") };
        fragmentShader.SetMacro("varying", "out");
        fragmentShader.SetMacro("attribute", "in");
        fragmentShader.Compile();

        program.AddShader(&texCoordShader);
        program.AddShader(&fragmentShader);
        program.Link();
        program.SetObjectLabel(name);
    };

    addShaderProgram(viewShaderProgram, "view");

    const auto addComputeShaderProgram = [](CStdGLShaderProgram &program, std::string_view name, std::string_view overrideImageFormat = "")
    {
        //static std::regex re_include{"^[ \\t]*#include[ \\t]+[<\"]([\\.\\w]+?)[>\"][ \\t]*$", regex_constants::optimize | regex_constants::ECMAScript};
        static std::regex localSizeRegex{"^[ \\t]*layout[ \\t]*\\([ \\t]*local_size_.*$", std::regex_constants::optimize | std::regex_constants::ECMAScript};
        static std::regex imageFormatRegex{"^[ \\t]*layout[ \\t]*\\([ \\t]*r[a-z0-9_]+.*$", std::regex_constants::optimize | std::regex_constants::ECMAScript};

        std::ifstream file;
        file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        file.open(std::string{"Shaders/"} + name.data()  + ".comp");
        file.exceptions(std::ifstream::badbit);

        std::ostringstream processedFile;

        std::string line;
        while (std::getline(file, line))
        {
            std::smatch match;
            if (std::regex_match(line, match, localSizeRegex))
            {
                processedFile << "layout(local_size_x=" << MainProgram::WorkGroupSize[0];
                processedFile << ", local_size_y=" << MainProgram::WorkGroupSize[1];
                processedFile << ", local_size_z=" << MainProgram::WorkGroupSize[2];
                processedFile << ") in;\n";
            }
            else if (!overrideImageFormat.empty() && std::regex_match(line, match, imageFormatRegex))
            {
                processedFile << "layout(" << overrideImageFormat << ')' << '\n';
            }
            else
            {
                processedFile << line << '\n';
            }
        }

        CStdGLShader computeShader{ CStdShader::Type::Compute, processedFile.str()};
        computeShader.Compile();

        program.AddShader(&computeShader);
        program.Link();
        program.SetObjectLabel(name);
    };

    addComputeShaderProgram(addImpulseShaderProgram, "add_impulse");
    addComputeShaderProgram(advectionShaderProgram, "advection");
    addComputeShaderProgram(jacobiShaderProgram, "jacobi");
    addComputeShaderProgram(divergenceShaderProgram, "divergence");
    addComputeShaderProgram(gradientShaderProgram, "gradient");
    addComputeShaderProgram(subtractShaderProgram, "subtract");
    addComputeShaderProgram(boundaryShaderProgram, "boundary");
    addComputeShaderProgram(copyShaderProgram, "copy");
    addComputeShaderProgram(clearShaderProgram, "clear");

    CStdGLShader renderPlaneVertexShader{CStdShader::Type::Vertex, LoadShader("Shaders/renderPlane.vert")};
    renderPlaneVertexShader.Compile();
    CStdGLShader renderPlaneFragmentShader{CStdShader::Type::Fragment, LoadShader("Shaders/renderPlane.frag")};
    renderPlaneFragmentShader.Compile();

    renderPlaneShaderProgram.AddShader(&renderPlaneVertexShader);
    renderPlaneShaderProgram.AddShader(&renderPlaneFragmentShader);
    renderPlaneShaderProgram.Link();
}

void MainProgram::BindImage(std::string_view name, const CStdTexture3D &texture, const int value, const GLenum access)
{
    static_cast<CStdGLShaderProgram *const>(CStdShaderProgram::GetCurrentShaderProgram())->SetUniform(name, glUniform1i, value);
    texture.BindImage(value, access);
}

void MainProgram::Compute()
{
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    // Workgroups (2048 CUDA cores)
    glDispatchCompute(cubeDimensions[0] / WorkGroupSize[0], cubeDimensions[1] / WorkGroupSize[1], cubeDimensions[2] / WorkGroupSize[2]);
}

void MainProgram::SolvePoissonSystem(CStdSwappableTexture3D &swappableTexture, const CStdTexture3D &initialValue, const float alpha, const float beta)
{
    CopyImage(initialValue, temporaryTexture);
    jacobiShaderProgram.Select();
    jacobiShaderProgram.SetUniform("alpha", alpha);
    jacobiShaderProgram.SetUniform("beta", beta);
    BindImage("fieldb_r", temporaryTexture, 0, GL_READ_ONLY);

    for (std::size_t i{0}; i < Variables::NumJacobiRounds; ++i)
    {
        BindImage("fieldx_r", swappableTexture.GetFront(), 1, GL_READ_ONLY);
        BindImage("field_out", swappableTexture.GetBack(), 2, GL_WRITE_ONLY);
        Compute();
        swappableTexture.SwapBuffers();
    }
}

void MainProgram::CopyImage(const CStdTexture3D &source, CStdTexture3D &destination)
{
    copyShaderProgram.Select();
    BindImage("src", source, 0, GL_READ_ONLY);
    BindImage("dest", destination, 1, GL_WRITE_ONLY);
    Compute();
}

void MainProgram::SetBounds(CStdSwappableTexture3D &texture, const float scale)
{
    boundaryShaderProgram.Select();
    boundaryShaderProgram.SetUniform("scale", scale);
    boundaryShaderProgram.SetUniform("box_size", glm::vec3{static_cast<float>(cubeDimensions[0]), static_cast<float>(cubeDimensions[1]), static_cast<float>(cubeDimensions[2])});
    Compute();
    texture.SwapBuffers();
}

void MainProgram::DoDroplets()
{
    static float acc{ 0.0f };
    static float nextDrop{ 0.0f };
    static glm::ivec2 frequencyRange{ 200, 1000 };

    acc += dt * 1000;
    if (acc >= nextDrop)
    {
        acc = 0;
        static constexpr auto Delay = 1000.0f;
        nextDrop = Delay + std::pow(-1, std::rand() % 2) * (std::rand() % static_cast<int>(0.5 * Delay));
        //LOG_INFO("Next drop: %.2f", next_drop);

        const glm::vec3 randomPositions[2]{ RandomPosition(), RandomPosition() };
        impulseState.LastPos = randomPositions[0];
        impulseState.CurrentPos = randomPositions[1];
        //std::cout << impulseState.LastPos.x << ' ' << impulseState.LastPos.y << ' ' << impulseState.LastPos.z << '\n';
        impulseState.Delta = impulseState.CurrentPos - impulseState.LastPos;
        impulseState.ForceActive = true;
        impulseState.InkActive = true;
        impulseState.Radial = true;
    }
    else
    {
        impulseState.ForceActive = false;
        impulseState.InkActive = false;
        impulseState.Radial = false;
    }
}

/*
void MainProgram::DoDroplets()
{
    static float acc = 0;
    static int next_drop = 0;
    static glm::ivec2 freq_range(30, 50);

    ++acc;

    if (acc > next_drop)
    {
        glm::vec3 rand_pos, rand_force;

        acc = 0;
        next_drop = (std::rand() % (freq_range.y - freq_range.x)) + freq_range.x;

        if ((std::rand() & 0x1) == 0x1)
        {
            rand_pos.x = std::rand() % cubeDimensions[0];
            rand_pos.y = cubeDimensions[1] / 2;
            rand_pos.z = std::rand() % cubeDimensions[2];

            rand_force.x = 0;
            rand_force.y = -1 * float(std::rand() % 1000) / 1000 * Variables::ForceMultiplier;
            rand_force.z = 0;
        }
        else
        {
            rand_pos.x = cubeDimensions[0] / 2;
            rand_pos.z = std::rand() % cubeDimensions[2];
            rand_pos.y = std::rand() % cubeDimensions[1];

            rand_force.x = -1 * float(std::rand() % 1000) / 1000 * Variables::ForceMultiplier;
            rand_force.y = 0;
            rand_force.z = 0;
        }

        impulseState.ForceActive = true;
        impulseState.InkActive = true;
        impulseState.Delta = rand_force;
        impulseState.CurrentPos = rand_pos;
    }
}
*/

glm::vec3 MainProgram::RandomPosition() const
{
    return { std::rand() % cubeDimensions[0], std::rand() % cubeDimensions[1], std::rand() % cubeDimensions[2] };
}