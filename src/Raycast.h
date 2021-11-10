#pragma once

#include <optional>

class Raycast
{
public:
	static std::optional<glm::vec3> GetLineIntersectionWithBox(const glm::vec3 &point, const glm::vec3 &direction);

private:
	static std::optional<glm::vec3> GetLineIntersectionWithRectangle();
	static std::optional<glm::vec3> GetLineIntersectionWithPlane();

private:
	static inline const std::array<glm::vec3, 8> CubeVertices
	{
		{-0.5f,  0.5f,  0.5f},
		{ 0.5f,  0.5f,  0.5f},
		{-0.5f,  0.5f, -0.5f},
		{ 0.5f,  0.5f, -0.5f},
		{-0.5f, -0.5f,  0.5f},
		{ 0.5f, -0.5f,  0.5f},
		{-0.5f, -0.5f, -0.5f},
		{ 0.5f, -0.5f, -0.5f}
	};
};