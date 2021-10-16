#include "GLFW.h"
#include "MainProgram.h"
#include "Shader.h"

#include <memory>

static constexpr auto Width = 800;
static constexpr auto Height = 800;
static constexpr auto CubeSize = 64;


static void DestroyGLFWWindow(GLFWwindow *const window)
{
    glfwDestroyWindow(window);
}

int main()
{
    //------------ GLFW init and config --------------
    GLFW glfw;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    std::unique_ptr<GLFWwindow, decltype(&DestroyGLFWWindow)> window{glfwCreateWindow(Width, Height, "FluidSimBachelorThesis3D", nullptr, nullptr), &DestroyGLFWWindow};
    if (!window)
    {
        throw std::runtime_error{"Failed to create window"};
    }

    glfwMakeContextCurrent(window.get());
    glfwSetWindowCloseCallback(window.get(), [](GLFWwindow *const wnd)
    {
        glfwSetWindowShouldClose(wnd, GLFW_TRUE);
    });

    //------------ GLAD OpenGL function pointers loader --------------
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
    {
        throw std::runtime_error{"Failed to initialize GLAD"};
    }

    if (!gladLoadGL())
    {
        throw std::runtime_error{"Failed to load GLAD"};
    }

    MainProgram mainProgram{window.get(), Width, Height, {CubeSize, CubeSize, CubeSize}};
    mainProgram.LoadShaders();
    mainProgram.Run();

    return 0;
}