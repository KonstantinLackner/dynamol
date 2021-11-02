#include "FluidSim.h"

using namespace gl;

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <filesystem>
#include <stdexcept>

namespace dynamol
{

FluidSim::FluidSim(Renderer *const renderer, const std::array<std::int32_t, 2> &windowDimensions, const std::array<std::int32_t, 3> &cubeDimensions)
	: m_renderer{renderer},
      m_timerQuery{std::make_unique<globjects::Query>()},
	  m_windowDimensions{windowDimensions},
	  m_cubeDimensions{cubeDimensions},
      m_velocityTexture{cubeDimensions[0], cubeDimensions[1], cubeDimensions[2], 4, true},
      m_pressureTexture{cubeDimensions[0], cubeDimensions[1], cubeDimensions[2], 4, true},
      m_divergenceTexture{cubeDimensions[0], cubeDimensions[1], cubeDimensions[2], 1, true},
	  m_temporaryTexture{cubeDimensions[0], cubeDimensions[1], cubeDimensions[2], 4, true},
      m_debugFramebuffer{windowDimensions[0], windowDimensions[1]},
      m_gridScale{1.0f / cubeDimensions[0]},
      m_splatRadius{cubeDimensions[0] * 0.37f},
      m_dt{0},
      m_lastTime{0},
      m_frameCounter{0},
      m_frameTimeSum{0}
{
    LoadShaders();
    m_debugFramebuffer.Bind();
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    m_debugFramebuffer.Unbind();
}

FluidSim::~FluidSim()
{
}

void FluidSim::Execute()
{
    const double now{glfwGetTime()};
    m_dt = m_lastTime == 0 ? 0.016667 : now - m_lastTime;
    m_lastTime = now;
    DoDroplets();

    m_timerQuery->begin(GL_TIME_ELAPSED);

#pragma region Advection
    m_advectionProgram->setUniform("delta_t", m_dt);
    m_advectionProgram->setUniform("dissipation", variables.Dissipation);
    m_advectionProgram->setUniform("gs", m_gridScale);
    m_advectionProgram->setUniform("gravity", variables.Gravity);
    BindImage(m_advectionProgram, "quantity_r", m_velocityTexture.GetFront(), 0, GL_READ_ONLY);
    BindImage(m_advectionProgram, "quantity_w", m_velocityTexture.GetBack(), 0, GL_WRITE_ONLY);
    BindImage(m_advectionProgram, "velocity", m_velocityTexture.GetFront(), 0, GL_READ_ONLY);
    Compute(m_advectionProgram);
    m_velocityTexture.SwapBuffers();
#pragma endregion

#pragma region Impulse
    if (m_impulseState.ForceActive)
    {
        m_addImpulseProgram->setUniform("position", m_impulseState.CurrentPos);
        m_addImpulseProgram->setUniform("radius", m_splatRadius);
        m_addImpulseProgram->setUniform("force", glm::vec4{ m_impulseState.Delta, 0 });
        BindImage(m_addImpulseProgram, "field_r", m_velocityTexture.GetFront(), 0, GL_READ_ONLY);
        BindImage(m_addImpulseProgram, "field_w", m_velocityTexture.GetBack(), 1, GL_WRITE_ONLY);
        Compute(m_addImpulseProgram);
        m_velocityTexture.SwapBuffers();
        m_impulseState.ForceActive = false;
    }

#pragma endregion

#pragma region Diffuse
    const float alpha{ (m_gridScale * m_gridScale) / (variables.Viscosity * m_dt) };
    const float beta{ alpha + 6.0f };
    SolvePoissonSystem(m_velocityTexture, m_velocityTexture.GetFront(), alpha, beta);
#pragma endregion

#pragma region Projection
    m_divergenceProgram->setUniform("gs", m_gridScale);
    BindImage(m_divergenceProgram, "field_r", m_velocityTexture.GetFront(), 0, GL_READ_ONLY);
    BindImage(m_divergenceProgram, "field_w", m_velocityTexture.GetBack(), 1, GL_WRITE_ONLY);
    Compute(m_divergenceProgram);

    SolvePoissonSystem(m_pressureTexture, m_velocityTexture.GetBack(), -1, 6.0f);

    m_gradientProgram->setUniform("gs", m_gridScale);
    BindImage(m_gradientProgram, "field_r", m_pressureTexture.GetFront(), 0, GL_READ_ONLY);
    BindImage(m_gradientProgram, "field_w", m_pressureTexture.GetBack(), 1, GL_WRITE_ONLY);
    Compute(m_gradientProgram);

    BindImage(m_subtractProgram, "a", m_velocityTexture.GetFront(), 0, GL_READ_ONLY);
    BindImage(m_subtractProgram, "b", m_pressureTexture.GetFront(), 1, GL_READ_ONLY);
    BindImage(m_subtractProgram, "c", m_velocityTexture.GetBack(), 2, GL_WRITE_ONLY);
    Compute(m_subtractProgram);
    m_velocityTexture.SwapBuffers();
#pragma endregion

#pragma region Bounds
    if (variables.Boundaries)
    {
        SetBounds(m_velocityTexture, -1);
    }
#pragma endregion
    // Transform feedback read
#pragma region TimeTrack
    m_timerQuery->end(GL_TIME_ELAPSED);

    const GLuint elapsedTime{m_timerQuery->waitAndGet(GL_QUERY_RESULT)};

    m_frameTimeSum += elapsedTime / 1000;

    if (++m_frameCounter == 100)
    {
        globjects::debug() << m_frameTimeSum / static_cast<float>(m_frameCounter) << '\n';
        m_frameCounter = 0;
        m_frameTimeSum = 0;
    }
#pragma endregion

#pragma region Render
    m_debugFramebuffer.Bind();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    m_renderPlaneProgram->use();
    m_velocityTexture.Bind(0);
    m_renderPlaneProgram->setUniform("sampler", 0);
    m_renderPlaneProgram->setUniform("depth", 0.0f);
    m_quad.Bind();

    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    m_quad.Draw();
    m_debugFramebuffer.Unbind();
#pragma endregion
}

GLuint FluidSim::GetDebugFramebufferTexture() const
{
    return m_debugFramebuffer.GetTexture().GetTexture();
}

const CStdTexture3D &FluidSim::GetVelocityTexture() const
{
    return m_velocityTexture.GetFront();
}

void FluidSim::LoadShaders()
{
    globjects::Shader::globalReplace("layout(local_size_x=1, local_size_y=1, local_size_z=1)", "layout(local_size_x=8, local_size_y=8, local_size_z=8)");

    const auto addShaderProgram = [this](globjects::Program *(FluidSim::*program), std::string_view name, std::initializer_list<std::pair<gl::GLenum, std::string>> shaders)
    {
        m_renderer->createShaderProgram(name.data(), shaders);
        this->*program = m_renderer->shaderProgram(name.data());
    };

    static constexpr std::array<std::pair<globjects::Program *(FluidSim::*), std::string_view>, 9> ProgramList
    {{
        {&FluidSim::m_addImpulseProgram, "add_impulse"},
        {&FluidSim::m_advectionProgram, "advection"},
        {&FluidSim::m_jacobiProgram, "jacobi"},
        {&FluidSim::m_divergenceProgram, "divergence"},
        {&FluidSim::m_gradientProgram, "gradient"},
        {&FluidSim::m_subtractProgram, "subtract"},
        {&FluidSim::m_boundaryProgram, "boundary"},
        {&FluidSim::m_copyProgram, "copy"},
        {&FluidSim::m_clearProgram, "clear"}
    }};

    for (const auto &[program, name] : ProgramList)
    {
        addShaderProgram(program, name, {{GL_COMPUTE_SHADER, std::string{"./fluidsim/shader/"} + name.data() + ".comp"}});
    }

    addShaderProgram(&FluidSim::m_renderPlaneProgram, "renderPlane",
    {
        {GL_VERTEX_SHADER, "fluidsim/shader/renderPlane.vert"},
        {GL_FRAGMENT_SHADER, "fluidsim/shader/renderPlane.frag"}
    });
}

void FluidSim::BindImage(globjects::Program *const program, std::string_view name, const CStdTexture3D& texture, const int value, const GLenum access)
{
    program->use();
    program->setUniform(name.data(), value);
    texture.BindImage(value, access);
}

void FluidSim::Compute(globjects::Program *const program)
{
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    // Workgroups (2048 CUDA cores)
    program->dispatchCompute(m_cubeDimensions[0] / WorkGroupSize[0], m_cubeDimensions[1] / WorkGroupSize[1], m_cubeDimensions[2] / WorkGroupSize[2]);
}

void FluidSim::SolvePoissonSystem(CStdSwappableTexture3D& swappableTexture, const CStdTexture3D& initialValue, const float alpha, const float beta)
{
    CopyImage(initialValue, m_temporaryTexture);
    m_jacobiProgram->setUniform("alpha", alpha);
    m_jacobiProgram->setUniform("beta", beta);
    BindImage(m_jacobiProgram, "fieldb_r", m_temporaryTexture, 0, GL_READ_ONLY);

    for (std::size_t i{ 0 }; i < variables.NumJacobiRounds; ++i)
    {
        BindImage(m_jacobiProgram, "fieldx_r", swappableTexture.GetFront(), 1, GL_READ_ONLY);
        BindImage(m_jacobiProgram, "field_out", swappableTexture.GetBack(), 2, GL_WRITE_ONLY);
        Compute(m_jacobiProgram);
        swappableTexture.SwapBuffers();
    }
}

void FluidSim::CopyImage(const CStdTexture3D& source, CStdTexture3D& destination)
{
    BindImage(m_copyProgram, "src", source, 0, GL_READ_ONLY);
    BindImage(m_copyProgram, "dest", destination, 1, GL_WRITE_ONLY);
    Compute(m_copyProgram);
}

void FluidSim::SetBounds(CStdSwappableTexture3D& texture, const float scale)
{
    m_boundaryProgram->setUniform("scale", scale);
    m_boundaryProgram->setUniform("box_size", glm::vec3{ static_cast<float>(m_cubeDimensions[0]), static_cast<float>(m_cubeDimensions[1]), static_cast<float>(m_cubeDimensions[2]) });
    BindImage(m_boundaryProgram, "field_r", texture.GetFront(), 0, GL_READ_ONLY);
    BindImage(m_boundaryProgram, "field_w", texture.GetBack(), 1, GL_WRITE_ONLY);
    Compute(m_boundaryProgram);
    m_velocityTexture.SwapBuffers();
}

void FluidSim::DoDroplets()
{
    static float acc{ 0.0f };
    static float nextDrop{ 0.0f };
    static glm::ivec2 frequencyRange{ 200, 1000 };

    acc += m_dt * 1000;
    if (acc >= nextDrop)
    {
        acc = 0;
        static constexpr auto Delay = 1000.0f;
        nextDrop = Delay + std::pow(-1, std::rand() % 2) * (std::rand() % static_cast<int>(0.5 * Delay));
        //LOG_INFO("Next drop: %.2f", next_drop);

        const glm::vec3 randomPositions[2]{ RandomPosition(), RandomPosition() };
        m_impulseState.LastPos = randomPositions[0];
        m_impulseState.CurrentPos = randomPositions[1];
        m_impulseState.Delta = m_impulseState.CurrentPos - m_impulseState.LastPos;
        m_impulseState.ForceActive = true;
        m_impulseState.InkActive = true;
        m_impulseState.Radial = true;
    }
    else
    {
        m_impulseState.ForceActive = false;
        m_impulseState.InkActive = false;
        m_impulseState.Radial = false;
    }
}

glm::vec3 FluidSim::RandomPosition() const
{
    return { std::rand() % m_cubeDimensions[0], std::rand() % m_cubeDimensions[1], std::rand() % m_cubeDimensions[2] };
}

}