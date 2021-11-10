#include "Raycast.h"

#include <cstddef>

std::optional<glm::vec3> Raycast::GetLineIntersectionWithBox(const glm::vec3 &point, const glm::vec3 &direction)
{
	std::size_t count{0};

	if (const auto intersection{GetLineIntersectionWithRectangle()	})
}