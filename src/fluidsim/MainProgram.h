#pragma once

#include "GLFW.h"
#include "ImpulseState.h"
#include "Shader.h"

#include <memory>

#include <glm/glm.hpp>

class MainProgram
{
public:
    MainProgram(GLFWwindow *const window, std::int32_t windowWidth, std::int32_t windowHeight, std::array<std::int32_t, 3> cubeDimensions);
    ~MainProgram();

public:
    void LoadShaders();
    void Run();

private:
    void Execute();

private:
    void BindImage(std::string_view name, const CStdTexture3D &texture, int value, GLenum access);
    void Compute();
    void SolvePoissonSystem(CStdSwappableTexture3D &swappableTexture, const CStdTexture3D &initialValue, float alpha, float beta);
    void CopyImage(const CStdTexture3D &source, CStdTexture3D &destination);
    void SetBounds(CStdSwappableTexture3D &texture, float scale);
    void DoDroplets();
    glm::vec3 RandomPosition() const;

private:
    CStdGLShaderProgram viewShaderProgram;
    CStdGLShaderProgram borderShaderProgram;
    CStdGLShaderProgram addImpulseShaderProgram;
    CStdGLShaderProgram advectionShaderProgram;
    CStdGLShaderProgram jacobiShaderProgram;
    CStdGLShaderProgram divergenceShaderProgram;
    CStdGLShaderProgram gradientShaderProgram;
    CStdGLShaderProgram subtractShaderProgram;
    CStdGLShaderProgram boundaryShaderProgram;
    CStdGLShaderProgram copyShaderProgram;
    CStdGLShaderProgram clearShaderProgram;
    CStdGLShaderProgram renderPlaneShaderProgram;

	CStdSwappableTexture3D velocityTexture;
	CStdSwappableTexture3D pressureTexture; // TODO: Pressure only needs 1 channel
	CStdTexture3D divergenceTexture;
	CStdTexture3D temporaryTexture;

    CStdRectangle quad;

private:
    static constexpr std::array<std::uint32_t, 3> WorkGroupSize{8, 8, 8};
    GLFWwindow *window;
    std::int32_t windowWidth;
    std::int32_t windowHeight;
    std::array<std::int32_t, 3> cubeDimensions;
    float dt;
    float gridScale;
    float splatRadius;
    float lastTime;
    ImpulseState impulseState;
    GLuint timerQuery;
    GLuint frameCounter;
    GLuint frameTimeSum;
};