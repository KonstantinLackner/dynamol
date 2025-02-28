#pragma once

#include <memory>
#include <optional>
#include <vector>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <imgui.h>

#include "Scene.h"
#include "Interactor.h"
#include "Renderer.h"
#include "FluidSim.h"

namespace dynamol
{

	class Viewer
	{
	public:
		Viewer(GLFWwindow* window, Scene* scene);
		void display();

		GLFWwindow * window();
		Scene* scene();
		FluidSim *fluidSim();

		glm::ivec2 viewportSize() const;

		glm::vec3 backgroundColor() const;
		glm::mat4 modelTransform() const;
		glm::mat4 viewTransform() const;
		glm::mat4 lightTransform() const;
		glm::mat4 projectionTransform() const;

		void setBackgroundColor(const glm::vec3& c);
		void setViewTransform(const glm::mat4& m);
		void setModelTransform(const glm::mat4& m);
		void setLightTransform(const glm::mat4& m);
		void setProjectionTransform(const glm::mat4& m);

		glm::mat4 modelViewTransform() const;
		glm::mat4 modelViewProjectionTransform() const;

		glm::mat4 modelLightTransform() const;
		glm::mat4 modelLightProjectionTransform() const;

		glm::mat4 viewProjectionTransform() const;

		glm::vec3 cameraPosition() const;
		void setCameraPosition(const glm::vec3 &cameraPosition);

		void saveImage(const std::string & filename);

	private:

		void beginFrame();
		void endFrame();
		void renderUi();
		void mainMenu();

		static void framebufferSizeCallback(GLFWwindow* window, int width, int height);
		static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
		static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
		static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
		static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);

		GLFWwindow* m_window;
		Scene *m_scene;

		std::vector<std::unique_ptr<Interactor>> m_interactors;
		std::vector<std::unique_ptr<Renderer>> m_renderers;
		FluidSim *m_fluidSim;

		glm::vec3 m_backgroundColor = glm::vec3(0.2f, 0.2f, 0.2f);
		glm::mat4 m_modelTransform = glm::mat4(1.0f);
		glm::mat4 m_viewTransform = glm::mat4(1.0f);
		glm::mat4 m_lightTransform = glm::mat4(1.0f);
		glm::mat4 m_projectionTransform = glm::mat4(1.0f);
		glm::vec3 m_cameraPosition;

		bool m_showUi = true;
		bool m_saveScreenshot = false;
	};


}