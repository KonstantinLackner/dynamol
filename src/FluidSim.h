#pragma once

#include "Renderer.h"
#include "Interactor.h"
#include "Shader.h"

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <tuple>
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
            float Dissipation{ 0.90f }; // { 0.995f };
            float Viscosity{ 0.05f }; // { 0.05f };
            bool Boundaries{ true };

            static constexpr std::size_t NumJacobiRounds{ 20 }; // 40
            static constexpr std::size_t NumJacobiRoundsDiffusion{ 10 }; // 20
        };

        struct VelocityVariables : public Variables
        {
            float Gravity{ 0.0f }; // { 8.0f };
            float GlobalGravity{ 0.0f };
            bool DoDroplets{ false };
            float ForceMultiplier{ 1.0f };
            float SplatRadius{ 20.0f };

            struct
            {
                std::int32_t Depth{ 0 };
                glm::vec2 Range { -128.0f, 128.0f };
            } DebugFramebuffer;
        };

        struct InkVariables : public Variables
        {
            bool DoInk { false };
            glm::vec4 InkColor{ 1.0f, 1.0f, 1.0f, 1.0f };
            float InkVolume { 20.0f };
            std::int32_t DebugFramebufferDepth { 0 };
        };

        struct Impulse
        {
            glm::vec3 Position;
            glm::vec3 Force;
        };

        struct TwoPointImpulse
        {
            std::pair<glm::vec3, glm::vec3> Positions;
            float ForceMultiplier;
        };

    public:
        FluidSim(Renderer *renderer, const std::array<std::int32_t, 2> &windowDimensions, const std::array<std::int32_t, 3> &cubeDimensions);
        ~FluidSim();

    public:
        void Execute();
        void DoInk();
        const CStdTexture3D &GetVelocityTexture() const;
        void DisplayDebugTextures();
        void AddImpulse(const Impulse &impulse);
        void AddTwoPointImpulse(const TwoPointImpulse &twoPointImpulse);

        virtual void keyEvent(int key, int scancode, int action, int mods) override;
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
        void DebugNanCheck(const CStdTexture3D &texture, GLuint depth);
        void CallDebugMethods(const CStdTexture3D& texture, const GLuint depth, std::string location);
        glm::vec3 RandomPosition() const;
        void RenderToDebugFramebuffer(std::string_view debugFramebuffer, const CStdTexture3D &texture);
        void RenderToDebugFramebuffer(std::string_view debugFramebuffer, const CStdTexture3D &texture, std::int32_t depth, const glm::vec2 &range);

    private:
        VelocityVariables m_variables;
        InkVariables m_inkVariables;
        Renderer *m_renderer;
        std::array<std::int32_t, 3> m_cubeDimensions;
        std::unique_ptr<globjects::Query> m_timerQuery;

        globjects::Program *m_borderProgram{nullptr};
        globjects::Program *m_addImpulseProgram{nullptr};
        globjects::Program *m_addTwoPointImpulseProgram{nullptr};
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

        CStdSwappableTexture3D m_inkTexture;

        std::map<std::string_view, std::pair<bool, CStdFramebuffer>> m_debugFramebuffers;
        CStdTexture3D m_debugBoundaryTexture;
        CStdRectangle m_quad;
        float m_dt;
        float m_gridScale;
        float m_lastTime;
        std::variant<std::monostate, Impulse, TwoPointImpulse> m_impulse;
        GLuint m_frameCounter;
        GLuint m_frameTimeSum;
        bool m_captureState;
        bool m_checkNan;
    };
}