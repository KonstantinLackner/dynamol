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
      m_velocityTexture{m_cubeDimensions[0], m_cubeDimensions[1], m_cubeDimensions[2], 4, false, true},
      m_pressureTexture{m_cubeDimensions[0], m_cubeDimensions[1], m_cubeDimensions[2], 4, false, true },
      m_divergenceTexture{m_cubeDimensions[0], m_cubeDimensions[1], m_cubeDimensions[2], 4, false, true },
	  m_temporaryTexture{m_cubeDimensions[0], m_cubeDimensions[1], m_cubeDimensions[2], 4, false, true },
      m_inkTexture{m_cubeDimensions[0], m_cubeDimensions[1], m_cubeDimensions[2], 4, false, true },
      m_debugBoundaryTexture{m_cubeDimensions[0], m_cubeDimensions[1], m_cubeDimensions[2], 4, false, true },
      m_gridScale{1.0f/128.f},
      m_dt{0},
      m_lastTime{0},
      m_mouseButtonPressed{false},
      m_wantsMouseInput{false},
      m_frameCounter{0},
      m_frameTimeSum{0},
      m_captureState{false},
      m_checkNan{false}
{
    m_debugFramebuffers.emplace(std::string_view{"Initial"}, CStdFramebuffer{windowDimensions[0], windowDimensions[1]});
    m_debugFramebuffers.emplace(std::string_view{"After Advection"}, CStdFramebuffer{windowDimensions[0], windowDimensions[1]});
    m_debugFramebuffers.emplace(std::string_view{"After Diffusion"}, CStdFramebuffer{windowDimensions[0], windowDimensions[1]});
    m_debugFramebuffers.emplace(std::string_view{"Result"}, CStdFramebuffer{windowDimensions[0], windowDimensions[1]});
    m_debugFramebuffers.emplace(std::string_view{"Boundary"}, CStdFramebuffer{windowDimensions[0], windowDimensions[1]});
    m_debugFramebuffers.emplace(std::string_view{"Ink"}, CStdFramebuffer{windowDimensions[0], windowDimensions[1]});

    m_velocityTexture.SetObjectLabel("velocity");
    m_pressureTexture.SetObjectLabel("pressure");
    m_divergenceTexture.SetObjectLabel("divergence");
    m_temporaryTexture.SetObjectLabel("temporary");
    m_inkTexture.SetObjectLabel("ink");
    m_debugBoundaryTexture.SetObjectLabel("debugBoundary");
    LoadShaders();

    for (const auto &it : m_debugFramebuffers)
    {
        it.second.Bind();
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        it.second.Unbind();
    }
}

FluidSim::~FluidSim()
{
}

void FluidSim::Execute()
{
    const double now{glfwGetTime()};
    m_dt = m_lastTime == 0 ? 0.016667 : now - m_lastTime;
    //m_dt = 0.02;
    m_lastTime = now;

    if (m_variables.DoDroplets)
    {
        DoDroplets();
    }

    m_timerQuery->begin(GL_TIME_ELAPSED);

    /*
    for (CStdTexture3D *const texture : {&m_pressureTexture.GetFront(), &m_pressureTexture.GetBack(), &m_velocityTexture.GetFront(), &m_velocityTexture.GetBack(), &m_divergenceTexture, &m_temporaryTexture})
    {
        texture->Clear();
    }
    */

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


    /*
        Debug Start
    */
    RenderToDebugFramebuffer("Initial", m_velocityTexture.GetFront());
    CallDebugMethods(m_velocityTexture.GetFront(), 50, "Advection");

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


    /*
        Debug Advection
    */
    RenderToDebugFramebuffer("After Advection", m_velocityTexture.GetFront());
    CallDebugMethods(m_velocityTexture.GetFront(), 50, "Diffuse");

#pragma region Diffuse
    const float alpha{ (m_gridScale * m_gridScale) / (m_variables.Viscosity * m_dt) };
    const float beta{ 1.0f / (6.0f + alpha) };
    SolvePoissonSystem(m_velocityTexture, m_velocityTexture.GetFront(), alpha, beta, false);
#pragma endregion
    /*
        Debug Diffuse
    */
    RenderToDebugFramebuffer("After Diffusion", m_velocityTexture.GetFront());
    CallDebugMethods(m_velocityTexture.GetFront(), 50, "Impulse");

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
            m_addImpulseProgram->setUniform("radius", m_variables.SplatRadius);
            m_addImpulseProgram->setUniform("forceMultiplier", m_variables.ForceMultiplier);
            m_addImpulseProgram->setUniform("force", glm::vec4{ impulseState.Delta, 0 });
            BindImage(m_addImpulseProgram, "field_r", m_velocityTexture.GetFront(), 0, GL_READ_ONLY);
            BindImage(m_addImpulseProgram, "field_w", m_velocityTexture.GetBack(), 1, GL_WRITE_ONLY);
            Compute(m_addImpulseProgram);
            m_velocityTexture.SwapBuffers();
        },
        [this](const std::pair<glm::vec3, glm::vec3> &impulseLine)
        {
            m_addImpulseLineProgram->setUniform("radius", m_variables.SplatRadius);
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

#pragma endregion

    /*
        Debug Impulse
    */
    CallDebugMethods(m_velocityTexture.GetFront(), 50, "Divergence");


#pragma region Gravity
    m_globalGravityProgram->setUniform("gravity", m_variables.GlobalGravity);
    BindImage(m_globalGravityProgram, "field_r", m_velocityTexture.GetFront(), 0, GL_READ_ONLY);
    BindImage(m_globalGravityProgram, "field_w", m_velocityTexture.GetBack(), 1, GL_WRITE_ONLY);
    Compute(m_globalGravityProgram);
    m_velocityTexture.SwapBuffers();
#pragma endregion


#pragma region Projection
    m_divergenceProgram->setUniform("gs", m_gridScale * 0.5f);
    BindImage(m_divergenceProgram, "field_r", m_velocityTexture.GetFront(), 0, GL_READ_ONLY);
    BindImage(m_divergenceProgram, "field_w", m_divergenceTexture, 1, GL_WRITE_ONLY);
    Compute(m_divergenceProgram);

    /*
        Debug Divergence
    */
    CallDebugMethods(m_velocityTexture.GetFront(), 50, "Pressurefield (pressureTexture.GetFront())");
      
    //pressuretexture nullsetzen
    m_pressureTexture.GetFront().Clear();
    m_pressureTexture.GetBack().Clear();

    // Pressurefield from which gradient is calculated
    SolvePoissonSystem(m_pressureTexture, m_divergenceTexture, -1, 1 / 6.0f, true); // Shouldn't this take 0.166666 = 1/6 as b?
    
    /*
        Debug Pressurefield
    */
    CallDebugMethods(m_pressureTexture.GetFront(), 50, "Gradient");

    // Calculate grad(P)
    m_gradientProgram->setUniform("gs", m_gridScale);
    BindImage(m_gradientProgram, "field_r", m_pressureTexture.GetFront(), 0, GL_READ_ONLY);
    BindImage(m_gradientProgram, "field_w", m_pressureTexture.GetBack(), 1, GL_WRITE_ONLY);
    Compute(m_gradientProgram);

    /*
        Debug Gradient
    */
    CallDebugMethods(m_pressureTexture.GetFront(), 50, "Boundaries (velocityTexture.GetFront())");

    if (m_variables.Boundaries)
    {
        SetBounds(m_velocityTexture, -1);
        RenderToDebugFramebuffer("Boundary", m_debugBoundaryTexture);
    }

    /*
        Debug Bounds
    */
    CallDebugMethods(m_velocityTexture.GetFront(), 50, "subtract result (velocityTexture.GetFront())");

    // Calculate U = W - grad(P) where div(U)=0
    BindImage(m_subtractProgram, "a", m_velocityTexture.GetFront(), 0, GL_READ_ONLY);
    BindImage(m_subtractProgram, "b", m_pressureTexture.GetBack(), 1, GL_READ_ONLY);
    BindImage(m_subtractProgram, "c", m_velocityTexture.GetBack(), 2, GL_WRITE_ONLY);
    Compute(m_subtractProgram);
    m_velocityTexture.SwapBuffers();

    /*
        Debug Subtract
    */
    CallDebugMethods(m_velocityTexture.GetFront(), 50, "END");

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

    RenderToDebugFramebuffer("Result", m_velocityTexture.GetFront());

    if (m_inkVariables.DoInk)
    {
        DoInk();
    }

    m_impulseState.emplace<std::monostate>();
}

void FluidSim::DoInk()
{
    m_advectionProgram->setUniform("delta_t", m_dt);
    m_advectionProgram->setUniform("dissipation", m_inkVariables.Dissipation);
    m_advectionProgram->setUniform("gs", m_gridScale);
    m_advectionProgram->setUniform("gravity", 0.0f);
    BindImage(m_advectionProgram, "quantity_r", m_inkTexture.GetFront(), 0, GL_READ_ONLY);
    BindImage(m_advectionProgram, "quantity_w", m_inkTexture.GetBack(), 1, GL_WRITE_ONLY);
    BindImage(m_advectionProgram, "velocity", m_velocityTexture.GetFront(), 2, GL_READ_ONLY);
    Compute(m_advectionProgram);
    m_inkTexture.SwapBuffers();

    const float alpha{ (m_gridScale * m_gridScale) / (m_inkVariables.Viscosity * m_dt) };
    const float beta{ 1.0f / (6.0f + alpha) };
    SolvePoissonSystem(m_inkTexture, m_inkTexture.GetFront(), alpha, beta, false);

    std::visit(Visitor{
        [this](ImpulseState &impulseState)
        {
            glm::vec4 color{m_inkVariables.RainbowMode ? impulseState.TickRainbowMode(m_dt) : m_inkVariables.InkColor };
            m_addImpulseProgram->setUniform("position", impulseState.CurrentPos);
            m_addImpulseProgram->setUniform("radius", m_inkVariables.InkVolume);
            m_addImpulseProgram->setUniform("forceMultiplier", 1.0f);
            m_addImpulseProgram->setUniform("force", color);
            BindImage(m_addImpulseProgram, "field_r", m_inkTexture.GetFront(), 0, GL_READ_ONLY);
            BindImage(m_addImpulseProgram, "field_w", m_inkTexture.GetBack(), 1, GL_WRITE_ONLY);
            Compute(m_addImpulseProgram);
            m_inkTexture.SwapBuffers();
        },
        [this](const std::pair<glm::vec3, glm::vec3> &impulseLine)
        {
            /*m_addImpulseLineProgram->setUniform("radius", m_inkVariables.InkVolume);
            m_addImpulseLineProgram->setUniform("start", impulseLine.first);
            m_addImpulseLineProgram->setUniform("end", impulseLine.second);
            m_addImpulseLineProgram->setUniform("cameraPosition", m_renderer->viewer()->cameraPosition());
            m_addImpulseLineProgram->setUniform("forceMultiplier", 1.0f);
            BindImage(m_addImpulseLineProgram, "field_r", m_inkTexture.GetFront(), 0, GL_READ_ONLY);
            BindImage(m_addImpulseLineProgram, "field_w", m_inkTexture.GetBack(), 1, GL_WRITE_ONLY);
            Compute(m_addImpulseLineProgram);
            m_inkTexture.SwapBuffers();*/
        },
        [this](std::monostate) {}
    }, m_impulseState);

    if (m_inkVariables.Boundaries)
    {
        SetBounds(m_inkTexture, 0);
    }

    static const glm::vec2 InkRange{0.0f, 1.0f};
    RenderToDebugFramebuffer("Ink", m_inkTexture.GetFront(), m_inkVariables.DebugFramebufferDepth, InkRange);
}

void FluidSim::CallDebugMethods(const CStdTexture3D& texture, const GLuint depth, std::string location) {
    if (location == "END") {
        m_captureState = false;
        m_checkNan = false;
    }
    else if (m_captureState) {
        DebugPrint(texture, depth);
        std::cout << "Now " << location << std::endl;
    }
    else if (m_checkNan) {
        DebugNanCheck(texture, depth);
        std::cout << "Now " << location << std::endl;
    }
}

void FluidSim::DebugPrint(const CStdTexture3D &texture, const GLuint depth)
{
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    const auto layer = texture.GetTextureImage(depth);
    const auto &dimensions = texture.GetDimensions();

    for (std::int32_t y{0}; y < dimensions[1]; ++y)
    {
        for (std::int32_t x{0}; x < dimensions[0]; ++x)
        {
            for (std::int32_t c{0}; c < 4; ++c)
            {
                std::cout << *(layer.get() + y * dimensions[0] + x * 4 + c) << ' ';
            }

            std::cout << " | ";
        }

        std::cout << '\n';
    }

    std::cout << std::endl;
}

void FluidSim::DebugNanCheck(const CStdTexture3D& texture, const GLuint depth)
{
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    const auto layer = texture.GetTextureImage(depth);
    const auto& dimensions = texture.GetDimensions();

    for (std::int32_t y{ 0 }; y < dimensions[1]; ++y)
    {
        for (std::int32_t x{ 0 }; x < dimensions[0]; ++x)
        {
            for (std::int32_t c{ 0 }; c < 4; ++c)
            {
                if (std::isnan(*(layer.get() + y * dimensions[0] + x * 4 + c)))
                {
                    __debugbreak();
                }
            }
        }
    }
}

const CStdTexture3D &FluidSim::GetVelocityTexture() const
{
    return m_velocityTexture.GetFront();
}

void FluidSim::DisplayDebugTextures()
{
    const auto display = [](std::string_view name, const CStdTexture<GL_TEXTURE_2D, 2> &texture)
    {
		ImGui::Begin(name.data());

		const std::uint64_t textureHandle{texture.GetTexture()};
		ImGui::Image(reinterpret_cast<ImTextureID>(textureHandle), ImGui::GetContentRegionAvail());

		ImGui::End();
    };

    for (const auto &[name, framebuffer] : m_debugFramebuffers)
    {
        display(name, framebuffer.GetTexture());
    }
}

void FluidSim::keyEvent(const int key, const int scancode, const int action, const int mods)
{
    if (key == GLFW_KEY_F10 && action == GLFW_RELEASE)
    {
        m_captureState = true;
    }
    else if (key == GLFW_KEY_F9 && action == GLFW_RELEASE) 
    {
        m_checkNan = true;
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
        int sliderFlags{0};
        static bool clamp{false};
        ImGui::Checkbox("Clamp", &clamp);
        if (clamp)
        {
            sliderFlags = ImGuiSliderFlags_AlwaysClamp;
        }

        ImGui::Checkbox("Do Droplets", &m_variables.DoDroplets);

        const auto defaultVariables = [sliderFlags, this](Variables& variables)
        {
            ImGui::SliderFloat("Dissipation", &variables.Dissipation, 0.9f, 1.0f, "%.5f", sliderFlags);
            ImGui::SliderFloat("Viscosity", &variables.Viscosity, 0.001f, 0.15f, "%.5f", sliderFlags);
            ImGui::Checkbox("Boundaries", &variables.Boundaries);
            ImGui::Separator();
        };

        if (ImGui::BeginMenu("Velocity"))
        {
            defaultVariables(m_variables);
            ImGui::SliderFloat("Gravity", &m_variables.Gravity, 0.0f, 30.0f, "%.3f", sliderFlags);
            ImGui::SliderFloat("GlobalGravity", &m_variables.GlobalGravity, 0.0f, 30.0f, "%.3f", sliderFlags);
            ImGui::SliderInt("Depth", &m_variables.DebugFramebuffer.Depth, 0, m_cubeDimensions[2] - 1, "%d", ImGuiSliderFlags_AlwaysClamp);
            ImGui::DragFloatRange2("Range", &m_variables.DebugFramebuffer.Range.x, &m_variables.DebugFramebuffer.Range.y, 1.0f, -128.0f, 128.0f, "%.3f", nullptr, ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("SplatRadius", &m_variables.SplatRadius, 1.0f, (m_cubeDimensions[0] / 5.0f) * 4.0f, "%.5f", sliderFlags);
		    ImGui::SliderFloat("ForceMultiplier", &m_variables.ForceMultiplier, 0.1f, 5.0f);

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Ink"))
        {
            ImGui::Checkbox("DoInk", &m_inkVariables.DoInk);

            if (!m_inkVariables.DoInk)
            {
                ImGui::BeginDisabled();
            }

            defaultVariables(m_inkVariables);
            ImGui::SliderInt("Depth", &m_inkVariables.DebugFramebufferDepth, 0, m_cubeDimensions[2] - 1, "%d", ImGuiSliderFlags_AlwaysClamp);
            ImGui::Separator();
            ImGui::Checkbox("RainbowMode", &m_inkVariables.RainbowMode);
            ImGui::SliderFloat("InkVolume", &m_inkVariables.InkVolume, 1.0f, (m_cubeDimensions[0] / 5.0f) * 4.0f, "%.5f", sliderFlags);
            ImGui::Separator();
            ImGui::ColorEdit4("InkColor", glm::value_ptr(m_inkVariables.InkColor));
            ImGui::Separator();

            if (!m_inkVariables.DoInk)
            {
                ImGui::EndDisabled();
            }

            ImGui::EndMenu();
        }

        ImGui::Separator();


        //ImGui::Separator();
        //ImGui::Checkbox("Mouse input", &m_wantsMouseInput);
		ImGui::EndMenu();
	}
}

void FluidSim::LoadShaders()
{
    static constexpr auto FormatString = "layout(local_size_x=%d, local_size_y=%d, local_size_z=%d)";
    std::array<char, 1024> buffer;
    std::snprintf(buffer.data(), buffer.size(), FormatString, Scene::WorkGroupSize[0], Scene::WorkGroupSize[1], Scene::WorkGroupSize[2]);
    globjects::Shader::globalReplace("layout(local_size_x=1, local_size_y=1, local_size_z=1)", buffer.data());
    globjects::Shader::globalReplace("layout(rgba16_snorm", "layout(rgba32f"); //potentially comment this out
    globjects::Shader::globalReplace("layout(r16_snorm", "layout(r32f");

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
    program->dispatchCompute(m_cubeDimensions[0] / Scene::WorkGroupSize[0], m_cubeDimensions[1] / Scene::WorkGroupSize[1], m_cubeDimensions[2] / Scene::WorkGroupSize[2]);
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
            BindImage(m_jacobiProgram, "fieldb_r", m_temporaryTexture, 0, GL_READ_ONLY);
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
    BindImage(m_boundaryProgram, "debug_w", m_debugBoundaryTexture, 2, GL_WRITE_ONLY);
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
        static constexpr auto Delay = 100.0f; // Should be 1000
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

void FluidSim::RenderToDebugFramebuffer(std::string_view debugFramebuffer, const CStdTexture3D &texture)
{
    RenderToDebugFramebuffer(debugFramebuffer, texture, m_variables.DebugFramebuffer.Depth, m_variables.DebugFramebuffer.Range);
}

void FluidSim::RenderToDebugFramebuffer(std::string_view debugFramebuffer, const CStdTexture3D &texture, const std::int32_t depth, const glm::vec2 &range)
{
    CStdFramebuffer &framebuffer{m_debugFramebuffers.at(debugFramebuffer)};

    framebuffer.Bind();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    m_renderPlaneProgram->use();
    texture.Bind(0);
    m_renderPlaneProgram->setUniform("depth", static_cast<float>(depth));
    m_renderPlaneProgram->setUniform("range", range);
    m_quad.Bind();

    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    m_quad.Draw();
    framebuffer.Unbind();
}

}