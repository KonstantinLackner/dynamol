#include "CameraInteractor.h"

#include <iostream>
#include <algorithm>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <glbinding/gl/gl.h>
#include <glbinding/gl/enum.h>
#include <glbinding/gl/functions.h>
#include <globjects/globjects.h>
#include <globjects/logging.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

#include "Viewer.h"

using namespace dynamol;
using namespace glm;
using namespace gl;

CameraInteractor::CameraInteractor(Viewer * viewer) : Interactor(viewer)
{
	resetProjectionTransform();
	resetViewTransform();

	globjects::debug() << "Camera interactor usage:";
	globjects::debug() << "  Drag left mouse - rotate";
	globjects::debug() << "  Drag middle mouse - pan";
	globjects::debug() << "  Drag right mouse - zoom";
	globjects::debug() << "  Shift + Left mouse - light position";
	globjects::debug() << "  H - toggle headlight";
	globjects::debug() << "  B - benchmark";
	globjects::debug() << "  Home - reset view";
	globjects::debug() << "  Cursor left - rotate negative around current y-axis";
	globjects::debug() << "  Cursor right - rotate positive around current y-axis";
	globjects::debug() << "  Cursor up - rotate negative around current x-axis";
	globjects::debug() << "  Cursor right - rotate positive around current x-axis" << std::endl;
}

void CameraInteractor::framebufferSizeEvent(int width, int height)
{
	float aspect = float(width) / float(height);

	if (m_perspective)
		viewer()->setProjectionTransform(perspective(m_fov, aspect, m_near, m_far));
	else
		viewer()->setProjectionTransform(ortho(-1.0f*aspect, 1.0f*aspect, -1.0f, 1.0f, m_near, m_far));
}

void CameraInteractor::keyEvent(int key, int scancode, int action, int mods)
{
	if (key == GLFW_KEY_LEFT_SHIFT && action == GLFW_PRESS)
	{
		m_light = true;
		m_xPrevious = m_xCurrent;
		m_yPrevious = m_yCurrent;
		cursorPosEvent(m_xCurrent, m_yCurrent);
	}
	else if (key == GLFW_KEY_LEFT_SHIFT && action == GLFW_RELEASE)
	{
		m_light = false;
	}
	else if (key == GLFW_KEY_HOME && action == GLFW_RELEASE)
	{
		resetViewTransform();
	}
	else if (key == GLFW_KEY_LEFT && action == GLFW_RELEASE)
	{
		mat4 viewTransform = viewer()->viewTransform();
		mat4 inverseViewTransform = inverse(viewTransform);
		vec4 transformedAxis = inverseViewTransform * vec4(0.0,1.0,0.0,0.0);

		mat4 newViewTransform = rotate(viewTransform, -0.5f*quarter_pi<float>(), vec3(transformedAxis));
		viewer()->setViewTransform(newViewTransform);
	}
	else if (key == GLFW_KEY_RIGHT && action == GLFW_RELEASE)
	{
		mat4 viewTransform = viewer()->viewTransform();
		mat4 inverseViewTransform = inverse(viewTransform);
		vec4 transformedAxis = inverseViewTransform * vec4(0.0, 1.0, 0.0, 0.0);

		mat4 newViewTransform = rotate(viewTransform, 0.5f*quarter_pi<float>(), vec3(transformedAxis));
		viewer()->setViewTransform(newViewTransform);
	}
	else if (key == GLFW_KEY_UP && action == GLFW_RELEASE)
	{
		mat4 viewTransform = viewer()->viewTransform();
		mat4 inverseViewTransform = inverse(viewTransform);
		vec4 transformedAxis = inverseViewTransform * vec4(1.0, 0.0, 0.0, 0.0);

		mat4 newViewTransform = rotate(viewTransform, -0.5f*quarter_pi<float>(), vec3(transformedAxis));
		viewer()->setViewTransform(newViewTransform);
	}
	else if (key == GLFW_KEY_DOWN && action == GLFW_RELEASE)
	{
		mat4 viewTransform = viewer()->viewTransform();
		mat4 inverseViewTransform = inverse(viewTransform);
		vec4 transformedAxis = inverseViewTransform * vec4(1.0, 0.0, 0.0, 0.0);

		mat4 newViewTransform = rotate(viewTransform, 0.5f*quarter_pi<float>(), vec3(transformedAxis));
		viewer()->setViewTransform(newViewTransform);
	}
	else if (key == GLFW_KEY_B && action == GLFW_RELEASE)
	{
		std::cout << "Starting benchmark" << std::endl;

		m_benchmark = true;
		m_startTime = glfwGetTime();
		m_frameCount = 0;
	}
}

void CameraInteractor::mouseButtonEvent(int button, int action, int mods)
{
	if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
	{
		m_rotating = true;
		m_xPrevious = m_xCurrent;
		m_yPrevious = m_yCurrent;
	}
	else if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS)
	{
		m_scaling = true;
		m_xPrevious = m_xCurrent;
		m_yPrevious = m_yCurrent;
	}
	else if (button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_PRESS)
	{
		m_panning = true;
		m_xPrevious = m_xCurrent;
		m_yPrevious = m_yCurrent;
	}
	else
	{
		m_rotating = false;
		m_scaling = false;
		m_panning = false;
	}
}

void CameraInteractor::cursorPosEvent(double xpos, double ypos)
{
	m_xCurrent = xpos;
	m_yCurrent = ypos;

	if (m_light)
	{
		vec3 v = arcballVector(m_xCurrent, m_yCurrent);
		mat4 viewTransform = viewer()->viewTransform();

		vec4 l = inverse(viewTransform) * vec4(v, 0.0);

		viewer()->setLightTransform(lookAt(vec3(l)*m_distance*0.5f, vec3(0.0f, 0.0f, 0.0f), vec3(0.0f, 1.0f, 0.0f)));

		//mat4 lightTransform = inverse(viewTransform)*viewer()->lightTransform()translate(mat4(1.0f), -0.5f*v*m_distance)*viewTransform;
		//viewer()->setLightTransform(lightTransform);
	}

	if (m_rotating)
	{
		if (m_xCurrent != m_xPrevious || m_yCurrent != m_yPrevious)
		{
			vec3 va = arcballVector(m_xPrevious, m_yPrevious);
			vec3 vb = arcballVector(m_xCurrent, m_yCurrent);

			if (va != vb)
			{
				float angle = acos(max(-1.0f, min(1.0f, dot(va, vb))));
				vec3 axis = cross(va, vb);

				mat4 viewTransform = viewer()->viewTransform();
				mat4 lightTransform = viewer()->lightTransform();
				mat4 inverseViewTransform = inverse(viewTransform);
				vec4 transformedAxis = inverseViewTransform * vec4(axis, 0.0);

				mat4 newViewTransform = rotate(viewTransform, angle, vec3(transformedAxis));
				viewer()->setViewTransform(newViewTransform);

				if (m_headlight)
				{
					mat4 newLightTransform = rotate(lightTransform, angle, vec3(transformedAxis));
					viewer()->setLightTransform(newLightTransform);
				}
			}
		}

	}

	if (m_scaling)
	{
		if (m_xCurrent != m_xPrevious || m_yCurrent != m_yPrevious)
		{
			ivec2 viewportSize = viewer()->viewportSize();
			vec2 va = vec2(2.0f*float(m_xPrevious) / float(viewportSize.x) - 1.0f, -2.0f*float(m_yPrevious) / float(viewportSize.y) + 1.0f);
			vec2 vb = vec2(2.0f*float(m_xCurrent) / float(viewportSize.x) - 1.0f, -2.0f*float(m_yCurrent) / float(viewportSize.y) + 1.0f);
			vec2 d = vb - va;

			float l = std::abs(d.x) > std::abs(d.y) ? d.x : d.y;
			float s = 1.0;

			if (l > 0.0f)
			{
				s += std::min(0.5f, length(d));
			}
			else
			{
				s -= std::min(0.5f, length(d));
			}

			mat4 viewTransform = viewer()->viewTransform();
			mat4 newViewTransform = scale(viewTransform, vec3(s, s, s));
			viewer()->setViewTransform(newViewTransform);
		}
	}

	if (m_panning)
	{
		if (m_xCurrent != m_xPrevious || m_yCurrent != m_yPrevious)
		{
			ivec2 viewportSize = viewer()->viewportSize();
			float aspect = float(viewportSize.x) / float(viewportSize.y);
			vec2 va = vec2(2.0f*float(m_xPrevious) / float(viewportSize.x) - 1.0f, -2.0f*float(m_yPrevious) / float(viewportSize.y) + 1.0f);
			vec2 vb = vec2(2.0f*float(m_xCurrent) / float(viewportSize.x) - 1.0f, -2.0f*float(m_yCurrent) / float(viewportSize.y) + 1.0f);
			vec2 d = vb - va;

			mat4 viewTransform = viewer()->viewTransform();
			mat4 newViewTransform = translate(mat4(1.0),vec3(aspect*d.x,d.y,0.0f))*viewTransform;
			viewer()->setViewTransform(newViewTransform);
		}
	}

	m_xPrevious = m_xCurrent;
	m_yPrevious = m_yCurrent;

	viewer()->setCameraPosition(arcballVector(m_xCurrent, m_yCurrent));

}

void CameraInteractor::scrollEvent(double xoffset, double yoffset)
{
	mat4 viewTransform = viewer()->viewTransform();
	mat4 newViewTransform = translate(mat4(1.0), vec3(0.0f, 0.0f, (yoffset / 8.0)))*viewTransform;
	viewer()->setViewTransform(newViewTransform);
}

void CameraInteractor::display()
{
	if (m_benchmark)
	{
		m_frameCount++;

		mat4 viewTransform = viewer()->viewTransform();
		mat4 inverseViewTransform = inverse(viewTransform);
		vec4 transformedAxis = inverseViewTransform * vec4(0.0, 1.0, 0.0, 0.0);

		mat4 newViewTransform = rotate(viewTransform, pi<float>() / 180.0f, vec3(transformedAxis));
		viewer()->setViewTransform(newViewTransform);


		if (m_frameCount >= 360)
		{
			double currentTime = glfwGetTime();

			std::cout << "Benchmark finished." << std::endl;
			std::cout << "Rendered " << m_frameCount << " frames in " << (currentTime - m_startTime) << " seconds." << std::endl;
			std::cout << "Average frames/second: " << double(m_frameCount) / (currentTime - m_startTime) << std::endl;

			m_benchmark = false;
		}


	}

	if (ImGui::BeginMenu("Camera"))
	{
		static int projection = 0;
		ImGui::RadioButton("Perspective", &projection, 0);
		ImGui::RadioButton("Orthographic", &projection, 1);

		if ((projection == 0) != m_perspective)
		{
			m_perspective = (projection == 0);
			resetProjectionTransform();
		}

		ImGui::Checkbox("Headlight", &m_headlight);
		ImGui::EndMenu();
	}
	
	if (m_light)
	{
		const mat4 modelLightMatrix = viewer()->modelLightTransform();
		const mat4 inverseModelLightMatrix = inverse(modelLightMatrix);
		vec4 worldLightPosition = inverseModelLightMatrix * vec4(0.0f, 0.0f, 0.0f, 1.0f);

		glDepthFunc(GL_ALWAYS);
		glMatrixMode(GL_PROJECTION);
		glLoadMatrixf(value_ptr(viewer()->projectionTransform()));

		glMatrixMode(GL_MODELVIEW);
		glLoadMatrixf(value_ptr(viewer()->modelViewTransform()));

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);
		glColor4f(1.0f, 1.0f, 1.0f, 0.5f);
		glPointSize(17.0);
		glBegin(GL_POINTS);
		glVertex3f(worldLightPosition.x, worldLightPosition.y, worldLightPosition.z);
		glEnd();
		glDisable(GL_BLEND);
	}

}

void CameraInteractor::resetProjectionTransform()
{
	vec2 viewportSize = viewer()->viewportSize();
	framebufferSizeEvent(viewportSize.x, viewportSize.y);
}

void CameraInteractor::resetViewTransform()
{
	viewer()->setViewTransform(lookAt(vec3(0.0f, 0.0f, -m_distance), vec3(0.0f, 0.0f, 0.0f), vec3(0.0f, 1.0f, 0.0f)));
	//viewer()->setLightTransform(lookAt(vec3(m_distance, m_distance, -m_distance), vec3(0.0f, 0.0f, 0.0f), vec3(0.0f, 1.0f, 0.0f)));
	viewer()->setLightTransform(lookAt(vec3(m_distance, m_distance, -m_distance), vec3(0.0f, 0.0f, 0.0f), vec3(0.0f, 1.0f, 0.0f)));
}

vec3 CameraInteractor::arcballVector(double x, double y)
{
	ivec2 viewportSize = viewer()->viewportSize();
	vec3 p = vec3(2.0f*float(x) / float(viewportSize.x)-1.0f, -2.0f*float(y) / float(viewportSize.y)+1.0f, 0.0);

	float length2 = p.x*p.x + p.y*p.y;

	if (length2 < 1.0f)
		p.z = sqrt(1.0f - length2);
	else
		p = normalize(p);

	return p;
}
