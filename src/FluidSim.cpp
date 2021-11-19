#include "FluidSim.h"
#include "Raycast.h"
#include "Viewer.h"

using namespace gl;

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <filesystem>
#include <stdexcept>

namespace
{
	template<typename... T> class Visitor : public T...
	{
	public:
		Visitor(T ...args) : T{args}... {}
		using T::operator()...;
	};
}

namespace dynamol
{

FluidSim::FluidSim(Renderer *const renderer, const std::array<std::int32_t, 2> &windowDimensions, const std::array<std::int32_t, 3> &cubeDimensions)
    : Interactor{renderer->viewer()},
      m_renderer{renderer},
      m_timerQuery{std::make_unique<globjects::Query>()},
	  m_windowDimensions{windowDimensions},
	  m_cubeDimensions{cubeDimensions},
      m_velocityTexture{cubeDimensions[0], cubeDimensions[1], cubeDimensions[2], 4, false},
      m_pressureTexture{cubeDimensions[0], cubeDimensions[1], cubeDimensions[2], 4, false},
      m_divergenceTexture{cubeDimensions[0], cubeDimensions[1], cubeDimensions[2], 1, false},
	  m_temporaryTexture{cubeDimensions[0], cubeDimensions[1], cubeDimensions[2], 4, false},
      m_debugFramebuffer{windowDimensions[0], windowDimensions[1]},
      m_gridScale{1.0f},
      m_splatRadius{cubeDimensions[0] * 0.37f},
      m_dt{0},
      m_lastTime{0},
      m_mouseButtonPressed{false},
      m_wantsMouseInput{false},
      m_frameCounter{0},
      m_frameTimeSum{0},
      m_captureState{false}
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

/*
#pragma region Seed
    BindImage(m_seedProgram, "field_w", m_velocityTexture.GetBack(), 1, GL_WRITE_ONLY);
    Compute(m_seedProgram);
    m_velocityTexture.SwapBuffers();
#pragma endregion
*/
    /*
#pragma region Bounds
    if (m_variables.Boundaries)
    {
        SetBounds(m_velocityTexture, -1);
    }
#pragma endregion
*/

#pragma region Advection
    m_advectionProgram->setUniform("delta_t", m_dt);
    m_advectionProgram->setUniform("dissipation", m_variables.Dissipation);
    m_advectionProgram->setUniform("gs", m_gridScale);
    m_advectionProgram->setUniform("gravity", m_variables.Gravity);
    BindImage(m_advectionProgram, "quantity_r", m_velocityTexture.GetFront(), 0, GL_READ_ONLY);
    BindImage(m_advectionProgram, "quantity_w", m_velocityTexture.GetBack(), 1, GL_WRITE_ONLY);
    BindImage(m_advectionProgram, "velocity", m_velocityTexture.GetFront(), 2, GL_READ_ONLY);
    Compute(m_advectionProgram);
    m_velocityTexture.SwapBuffers();
#pragma endregion

    
#pragma region Diffuse
    const float alpha{ (m_gridScale * m_gridScale) / (m_variables.Viscosity * m_dt) };
    const float beta{ 1.0f / (4.0f + alpha) };
    SolvePoissonSystem(m_velocityTexture, m_velocityTexture.GetFront(), alpha, beta, false);
#pragma endregion
    if (m_captureState) {
        DebugPrint(m_velocityTexture.GetFront(), 0);
        m_captureState = false;
    }

#pragma region Impulse

    if (m_mouseButtonPressed)
    {
        Viewer *const viewer{m_renderer->viewer()};
        double x;
        double y;
        glfwGetCursorPos(viewer->window(), &x, &y);
        y = m_windowDimensions[1] - y;

        const auto halfWindowWidth = static_cast<double>(m_windowDimensions[0]) / 2;
        const auto halfWindowHeight = static_cast<double>(m_windowDimensions[1]) / 2;

        const glm::vec4 screenCoords{
            static_cast<float>((x - halfWindowWidth) / halfWindowWidth),
            static_cast<float>((y - halfWindowHeight) / halfWindowWidth),
            -2,
            1
            };

        glm::vec4 pickCoords{glm::inverse(viewer->viewProjectionTransform()) * screenCoords};
        pickCoords /= pickCoords.w;

        const glm::vec3 cameraPosition{viewer->cameraPosition()};
        const auto delta = glm::vec3{pickCoords.x, pickCoords.y, pickCoords.z} - cameraPosition;
        const auto direction = glm::normalize(delta);

        if (const auto intersections = Raycast::GetLineIntersectionsWithBox(cameraPosition, direction); intersections)
        {
            const glm::vec3 cubeSize{m_cubeDimensions[0], m_cubeDimensions[1], m_cubeDimensions[2]};
            m_impulseState.emplace<2>(std::make_pair((intersections->first + 0.5f) * cubeSize, (intersections->second + 0.5f) * cubeSize));
        }
    }

    std::visit(Visitor{
        [this](const ImpulseState &impulseState)
        {
            m_addImpulseProgram->setUniform("position", impulseState.CurrentPos);
            m_addImpulseProgram->setUniform("radius", m_splatRadius);
            m_addImpulseProgram->setUniform("force", glm::vec4{ impulseState.Delta, 0 });
            BindImage(m_addImpulseProgram, "field_r", m_velocityTexture.GetFront(), 0, GL_READ_ONLY);
            BindImage(m_addImpulseProgram, "field_w", m_velocityTexture.GetBack(), 1, GL_WRITE_ONLY);
            Compute(m_addImpulseProgram);
            m_velocityTexture.SwapBuffers();
        },
        [this](const std::pair<glm::vec3, glm::vec3> &impulseLine)
        {
            m_addImpulseLineProgram->setUniform("radius", m_splatRadius);
            m_addImpulseLineProgram->setUniform("start", impulseLine.first);
            m_addImpulseLineProgram->setUniform("end", impulseLine.second);
            m_addImpulseLineProgram->setUniform("cameraPosition", m_renderer->viewer()->cameraPosition());
            m_addImpulseLineProgram->setUniform("forceMultiplier", m_variables.ForceMultiplier);
            BindImage(m_addImpulseLineProgram, "field_r", m_velocityTexture.GetFront(), 0, GL_READ_ONLY);
            BindImage(m_addImpulseLineProgram, "field_w", m_velocityTexture.GetBack(), 1, GL_WRITE_ONLY);
            Compute(m_addImpulseLineProgram);
            m_velocityTexture.SwapBuffers();
        },
        [this](std::monostate) {}
    }, m_impulseState);

    m_impulseState.emplace<0>();

#pragma endregion

/*
#pragma region Gravity
    m_globalGravityProgram->setUniform("gravity", m_variables.GlobalGravity);
    BindImage(m_globalGravityProgram, "field_r", m_velocityTexture.GetFront(), 0, GL_READ_ONLY);
    BindImage(m_globalGravityProgram, "field_w", m_velocityTexture.GetBack(), 1, GL_WRITE_ONLY);
    Compute(m_globalGravityProgram);
    m_velocityTexture.SwapBuffers();
#pragma endregion
*/


#pragma region Projection
    m_divergenceProgram->setUniform("gs", m_gridScale * 0.5f);
    BindImage(m_divergenceProgram, "field_r", m_velocityTexture.GetFront(), 0, GL_READ_ONLY);
    BindImage(m_divergenceProgram, "field_w", m_velocityTexture.GetBack(), 1, GL_WRITE_ONLY);
    Compute(m_divergenceProgram);
      
    //pressuretexture nullsetzen
    m_pressureTexture.GetFront().Clear();
    m_pressureTexture.GetBack().Clear();

    // Pressurefield from which gradient is calculated
    SolvePoissonSystem(m_pressureTexture, m_velocityTexture.GetBack(), -m_gridScale * m_gridScale, 0.25f, true); // Shouldn't this take velocity.Back.x as b?
    
    // Calculate grad(P)
    m_gradientProgram->setUniform("gs", m_gridScale);
    BindImage(m_gradientProgram, "field_r", m_pressureTexture.GetFront(), 0, GL_READ_ONLY);
    BindImage(m_gradientProgram, "field_w", m_pressureTexture.GetBack(), 1, GL_WRITE_ONLY);
    Compute(m_gradientProgram);

    if (m_variables.Boundaries)
    {
        SetBounds(m_velocityTexture, -1);
    }

    // Calculate U = W - grad(P) where div(U)=0
    BindImage(m_subtractProgram, "a", m_velocityTexture.GetFront(), 0, GL_READ_ONLY);
    BindImage(m_subtractProgram, "b", m_pressureTexture.GetBack(), 1, GL_READ_ONLY);
    BindImage(m_subtractProgram, "c", m_velocityTexture.GetBack(), 2, GL_WRITE_ONLY);
    Compute(m_subtractProgram);
    m_velocityTexture.SwapBuffers();
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
    // Zu Debugmethode um dann damit zu prüfen, ob bspw. divergence 0, oder wie die einzelnen werte ausschauenS
    m_debugFramebuffer.Bind();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    m_renderPlaneProgram->use();
    m_velocityTexture.Bind(0);
    m_renderPlaneProgram->setUniform("depth", static_cast<float>(m_variables.DebugFramebuffer.Depth));
    m_renderPlaneProgram->setUniform("range", m_variables.DebugFramebuffer.Range);
    m_quad.Bind();

    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    m_quad.Draw();
    m_debugFramebuffer.Unbind();
#pragma endregion
}

void FluidSim::DebugPrint(const CStdTexture3D &texture, const GLuint depth)
{
    const auto layer = texture.GetTextureImage(depth);
    const auto &dimensions = texture.GetDimensions();

    for (std::int32_t y{0}; y < dimensions[1]; ++y)
    {
        for (std::int32_t x{0}; x < dimensions[0]; ++x)
        {
            for (std::int32_t c{0}; c < 4; ++c)
            {
                std::cout << *(layer.get() + y * dimensions[1] + x * dimensions[0] + c) << ' ';
            }

            std::cout << " | ";
        }

        std::cout << '\n';
    }

    std::cout << std::endl;
}


GLuint FluidSim::GetDebugFramebufferTexture() const
{
    return m_debugFramebuffer.GetTexture().GetTexture();
}

const CStdTexture3D &FluidSim::GetVelocityTexture() const
{
    return m_velocityTexture.GetFront();
}

void FluidSim::keyEvent(const int key, const int scancode, const int action, const int mods)
{
    if (key == GLFW_KEY_F10 && action == GLFW_RELEASE)
    {
        m_captureState = true;
    }
}

void FluidSim::mouseButtonEvent(const int button, const int action, const int mods)
{
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    m_mouseButtonPressed = action == GLFW_PRESS;
}

void FluidSim::cursorPosEvent(const double x, const double y)
{
    //m_mouseButtonPressed = WantsMouseInput() && glfwGetMouseButton(m_renderer->viewer()->window(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
}

void FluidSim::display()
{
    if (ImGui::BeginMenu("FluidSim"))
    {
		//ImGui::ColorEdit3("Background", glm::value_ptr(m_backgroundColor));
		ImGui::SliderFloat("Dissipation", &m_variables.Dissipation, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Gravity", &m_variables.Gravity, 0.0f, 30.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Viscosity", &m_variables.Viscosity, 0.0001f, 10.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);

		ImGui::SliderFloat("ForceMultiplier", &m_variables.ForceMultiplier, 0.1f, 10.0f);
		ImGui::Checkbox("Boundaries", &m_variables.Boundaries);
		ImGui::SliderFloat("Global Gravity", &m_variables.GlobalGravity, 0.f, 10.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);

        ImGui::Separator();
        ImGui::SliderInt("Depth", &m_variables.DebugFramebuffer.Depth, 0, m_cubeDimensions[2] - 1, "%d", ImGuiSliderFlags_AlwaysClamp);
        ImGui::DragFloatRange2("Range", &m_variables.DebugFramebuffer.Range.x, &m_variables.DebugFramebuffer.Range.y, 1.0f, -10.0f, 10.0f, "%.3f", nullptr, ImGuiSliderFlags_AlwaysClamp);

        //ImGui::Separator();
        //ImGui::Checkbox("Mouse input", &m_wantsMouseInput);
		ImGui::EndMenu();
	}
}

void FluidSim::LoadShaders()
{
    globjects::Shader::globalReplace("layout(local_size_x=1, local_size_y=1, local_size_z=1)", "layout(local_size_x=8, local_size_y=8, local_size_z=8)");
    globjects::Shader::globalReplace("layout(rgba16_snorm)", "layout(rgba16f)");
    globjects::Shader::globalReplace("layout(r16_snorm)", "layout(r16f)");

    const auto addShaderProgram = [this](globjects::Program *(FluidSim::*program), std::string_view name, std::initializer_list<std::pair<gl::GLenum, std::string>> shaders)
    {
        m_renderer->createShaderProgram(name.data(), shaders);
        this->*program = m_renderer->shaderProgram(name.data());
    };

    static constexpr std::array<std::pair<globjects::Program *(FluidSim::*), std::string_view>, 12> ProgramList
    {{
        {&FluidSim::m_addImpulseProgram, "add_impulse"},
        {&FluidSim::m_addImpulseLineProgram, "add_impulse_line"},
        {&FluidSim::m_advectionProgram, "advection"},
        {&FluidSim::m_jacobiProgram, "jacobi"},
        {&FluidSim::m_divergenceProgram, "divergence"},
        {&FluidSim::m_gradientProgram, "gradient"},
        {&FluidSim::m_subtractProgram, "subtract"},
        {&FluidSim::m_boundaryProgram, "boundary"},
        {&FluidSim::m_copyProgram, "copy"},
        {&FluidSim::m_clearProgram, "clear"},
        {&FluidSim::m_globalGravityProgram, "global_gravity"},
        {&FluidSim::m_seedProgram, "seed"}
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

void FluidSim::SolvePoissonSystem(CStdSwappableTexture3D& swappableTexture, const CStdTexture3D& initialValue, const float alpha, const float beta, const bool isProject)
{
    CopyImage(initialValue, m_temporaryTexture);
    m_jacobiProgram->setUniform("alpha", alpha);
    m_jacobiProgram->setUniform("beta", beta);
    BindImage(m_jacobiProgram, "fieldb_r", m_temporaryTexture, 0, GL_READ_ONLY);
    for (std::size_t i{ 0 }; i < (isProject ? Variables::NumJacobiRounds : Variables::NumJacobiRoundsDiffusion); ++i)
    {

        if (isProject) {
            SetBounds(swappableTexture, 1);
        }

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
    // m_boundaryProgram->setUniform("box_size", glm::vec3{ static_cast<float>(m_cubeDimensions[0]), static_cast<float>(m_cubeDimensions[1]), static_cast<float>(m_cubeDimensions[2]) });
    BindImage(m_boundaryProgram, "field_r", texture.GetFront(), 0, GL_READ_ONLY);
    BindImage(m_boundaryProgram, "field_w", texture.GetBack(), 1, GL_WRITE_ONLY);
    Compute(m_boundaryProgram);
    texture.SwapBuffers();
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
        static constexpr auto Delay = 10.0f; // Should be 1000
        nextDrop = Delay + std::pow(-1, std::rand() % 2) * (std::rand() % static_cast<int>(0.5 * Delay));
        //LOG_INFO("Next drop: %.2f", next_drop);

        const glm::vec3 randomPositions[2]{ RandomPosition(), RandomPosition() };
        ImpulseState impulseState;
        impulseState.LastPos = randomPositions[0];
        impulseState.CurrentPos = randomPositions[1];
        impulseState.Delta = impulseState.CurrentPos - impulseState.LastPos;
        //globjects::debug() << impulseState.Delta.x << ',' << impulseState.Delta.y << ',' << impulseState.Delta.z << ',' <<'\n';
        impulseState.ForceActive = true;
        impulseState.InkActive = true;
        impulseState.Radial = true;

        m_impulseState.emplace<ImpulseState>(impulseState);
    }
}

glm::vec3 FluidSim::RandomPosition() const
{
    return { std::rand() % m_cubeDimensions[0], std::rand() % m_cubeDimensions[1], std::rand() % m_cubeDimensions[2] };
}

}