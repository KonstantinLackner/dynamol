#pragma once
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

class GLFWError : public std::runtime_error
{
public:
    using runtime_error::runtime_error;
};

class GLFW sealed
{
public:
    GLFW()
    {
        glfwSetErrorCallback([](int code, const char* message)
            {
                throw GLFWError{ std::string{message} + "(code: " + std::to_string(code) + ')' };
            });

        if (int code{ glfwInit() }; !code)
        {
            throw GLFWError{ std::string{"glfwInit failed (code: "} + std::to_string(code) + ')' };
        }
        Active = true;
    }

    ~GLFW()
    {
        glfwTerminate();
        Active = false;
    }

public:
    inline static bool Active{ false };
};