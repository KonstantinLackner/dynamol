#pragma once

#include "Renderer.h"
#include "ImpulseState.h"
#include "Interactor.h"
#include "Shader.h"

#include <array>
#include <memory>
#include <variant>

#include <glm/glm.hpp>
#include <glbinding/gl/gl.h>
#include <glbinding/gl/enum.h>
#include <glbinding/gl/functions.h>

#include <globjects/VertexArray.h>
#include <globjects/VertexAttributeBinding.h>
#include <globjects/Buffer.h>
#include <globjects/Program.h>
#include <globjects/Shader.h>
#include <globjects/Framebuffer.h>
#include <globjects/Renderbuffer.h>
#include <globjects/Texture.h>
#include <globjects/base/File.h>
#include <globjects/TextureHandle.h>
#include <globjects/NamedString.h>
#include <globjects/base/StaticStringSource.h>
#include <globjects/Query.h>
#include <globjects/TransformFeedback.h>

namespace dynamol
{
    class FluidSim : public Interactor
    {
    public:
        struct Variables
        {
            float Dissipation{ 0.99f };
            float Gravity{ 8.0f };
            float Viscosity{ 0.0004 };
            float GlobalGravity{ 0.f };
            float ForceMulti{ 1.f };
            bool HasSeeded{ false };
            bool Boundaries{ true };
            float ForceMultiplier{ 1.0f };

            struct
            {
                std::int32_t Depth{ 0 };
                glm::vec2 Range { -10.0f, 10.0f };
            } DebugFramebuffer;

            static constexpr std::size_t NumJacobiRounds{ 40 };
            static constexpr std::size_t NumJacobiRoundsDiffusion{ 20 };
        };

    public:
        FluidSim(Renderer *renderer, const std::array<std::int32_t, 2> &windowDimensions, const std::array<std::int32_t, 3> &cubeDimensions);
        ~FluidSim();

    public:
        void Execute();
        GLuint GetDebugFramebufferTexture() const;
        const CStdTexture3D &GetVelocityTexture() const;
        bool WantsMouseInput() const { return m_wantsMouseInput; }

        virtual void keyEvent(int key, int scancode, int action, int mods) override;
		virtual void mouseButtonEvent(int button, int action, int mods) override;
        virtual void cursorPosEvent(double x, double y) override;
        virtual void display() override;

    private:
        void LoadShaders();
        void BindImage(globjects::Program *program, std::string_view name, const CStdTexture3D &texture, int value, GLenum access);
        void Compute(globjects::Program *program);
        void SolvePoissonSystem(CStdSwappableTexture3D &swappableTexture, const CStdTexture3D &initialValue, float alpha, float beta, bool isProject);
        void CopyImage(const CStdTexture3D &source, CStdTexture3D &destination);
        void SetBounds(CStdSwappableTexture3D &texture, float scale);
        void DoDroplets();
        void DebugPrint(const CStdTexture3D &texture, GLuint depth);
        glm::vec3 RandomPosition() const;

    private:
        static constexpr std::array<int32_t, 3> WorkGroupSize{8, 8, 8};
        Variables m_variables;
        Renderer *m_renderer;
        std::array<std::int32_t, 2> m_windowDimensions;
        std::array<std::int32_t, 3> m_cubeDimensions;
        std::unique_ptr<globjects::Query> m_timerQuery;

        globjects::Program *m_borderProgram{nullptr};
        globjects::Program *m_addImpulseProgram{nullptr};
        globjects::Program *m_addImpulseLineProgram{nullptr};
        globjects::Program *m_advectionProgram{nullptr};
        globjects::Program *m_jacobiProgram{nullptr};
        globjects::Program *m_divergenceProgram{nullptr};
        globjects::Program *m_gradientProgram{nullptr};
        globjects::Program *m_subtractProgram{nullptr};
        globjects::Program *m_boundaryProgram{nullptr};
        globjects::Program *m_copyProgram{nullptr};
        globjects::Program *m_clearProgram{nullptr};
        globjects::Program *m_renderPlaneProgram{nullptr};
        globjects::Program *m_globalGravityProgram{nullptr};
        globjects::Program *m_seedProgram{nullptr};

        CStdSwappableTexture3D m_velocityTexture;
        CStdSwappableTexture3D m_pressureTexture; // TODO: Pressure only needs 1 channel
        CStdTexture3D m_divergenceTexture;
        CStdTexture3D m_temporaryTexture;
        CStdFramebuffer m_debugFramebuffer;
        CStdRectangle m_quad;
        float m_dt;
        float m_gridScale;
        float m_splatRadius;
        float m_lastTime;
        std::variant<std::monostate, ImpulseState, std::pair<glm::vec3, glm::vec3>> m_impulseState;
        bool m_mouseButtonPressed;
        bool m_wantsMouseInput;
        GLuint m_frameCounter;
        GLuint m_frameTimeSum;
        bool m_captureState;
    };
}