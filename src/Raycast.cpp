#include "Raycast.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>

std::optional<std::pair<glm::vec3, glm::vec3>> Raycast::GetLineIntersectionsWithBox(const glm::vec3 &point, const glm::vec3 &direction)
{
	std::size_t count{0};

	std::array<glm::vec3, 2> intersections;
	std::optional<glm::vec3> currentIntersection;

#define CHECK(_a, _b, _c, _d) \
		currentIntersection = GetLineIntersectionWithRectangle(point, direction, CubeVertices[_a], CubeVertices[_b], CubeVertices[_c], CubeVertices[_d]); \
		if (currentIntersection) \
		{ \
			intersections[count++] = *currentIntersection; \
			if (count == 2) \
			{ \
				if (glm::length(point - intersections[0]) <= glm::length(point - intersections[1])) \
				{ \
					return {{intersections[0], intersections[1]}}; \
				} \
				else \
				{ \
					return {{intersections[1], intersections[0]}}; \
				} \
			} \
		}

	CHECK(2, 3, 6, 7)
	CHECK(0, 1, 4, 5)
	CHECK(0, 2, 4, 6)
	CHECK(3, 1, 7, 5)
	CHECK(0, 1, 2, 3)
	CHECK(4, 5, 6, 7)

#undef CHECK

	return {};
}

std::optional<glm::vec3> Raycast::GetLineIntersectionWithRectangle(const glm::vec3 &point, const glm::vec3 &direction, const glm::vec3 &topLeft, const glm::vec3 &topRight, const glm::vec3 &bottomLeft, const glm::vec3 &bottomRight)
{
	const auto height = bottomLeft + 0.5f * (topLeft - bottomLeft);
	const auto center = height + 0.5f * (topRight - topLeft);
	const auto normal = glm::normalize(glm::cross(topLeft - bottomRight, topRight - bottomLeft));

	const auto x = GetLineIntersectionWithPlane(point, direction, center, normal);
	if (!x)
	{
		return {};
	}

	const auto u = topLeft - topRight;
	const auto v = topLeft - bottomRight;
	const auto dotu = glm::dot(u, *x);
	const auto dotv = glm::dot(v, *x);
	const auto dotuTopLeft = glm::dot(u, topLeft);
	const auto dotuTopRight = glm::dot(u, topRight);
	const auto dotvTopLeft = glm::dot(v, topLeft);
	const auto dotvBottomLeft = glm::dot(v, bottomLeft);

	const auto isBetween = [](const float x, const float a, const float b)
	{
		if (a < b)
		{
			return x >= a && x <= b;
		}
		if (a > b)
		{
			return x >= b && x <= a;
		}

		return false;
	};

	if (isBetween(dotu, dotuTopLeft, dotuTopRight) && isBetween(dotv, dotvTopLeft, dotvBottomLeft))
	{
		return x;
	}

	return {};
}

std::optional<glm::vec3> Raycast::GetLineIntersectionWithPlane(const glm::vec3 &point, const glm::vec3 &direction, const glm::vec3 &planePoint, const glm::vec3 &planeNormal)
{
	static constexpr auto Epsilon = std::numeric_limits<glm::vec3::value_type>::epsilon();

	const auto numerator = glm::dot(planePoint - point, planeNormal);
	if (std::abs(numerator) < Epsilon) // Line is on the plane
	{
		return {};
	}

	const auto denominator = glm::dot(direction, planeNormal);
	if (std::abs(denominator) < Epsilon) // Line is parallel to plane
	{
		return {};
	}

	return {point + (numerator / denominator) * direction};
}