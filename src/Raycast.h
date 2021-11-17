#pragma once

#include <array>
#include <optional>
#include <tuple>

#include <glm/glm.hpp>

class Raycast
{
public:
	static std::optional<std::pair<glm::vec3, glm::vec3>> GetLineIntersectionsWithBox(const glm::vec3 &point, const glm::vec3 &direction);

private:
	static std::optional<glm::vec3> GetLineIntersectionWithRectangle(const glm::vec3 &point, const glm::vec3 &direction, const glm::vec3 &topLeft, const glm::vec3 &topRight, const glm::vec3 &bottomLeft, const glm::vec3 &bottomRight);
	static std::optional<glm::vec3> GetLineIntersectionWithPlane(const glm::vec3 &point, const glm::vec3 &direction, const glm::vec3 &planePoint, const glm::vec3 &planeNormal);

private:
	static inline const std::array<glm::vec3, 8> CubeVertices
	{{
		{-0.5f,  0.5f,  0.5f},
		{ 0.5f,  0.5f,  0.5f},
		{-0.5f,  0.5f, -0.5f},
		{ 0.5f,  0.5f, -0.5f},
		{-0.5f, -0.5f,  0.5f},
		{ 0.5f, -0.5f,  0.5f},
		{-0.5f, -0.5f, -0.5f},
		{ 0.5f, -0.5f, -0.5f}
	}};
};