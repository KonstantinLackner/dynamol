#pragma once

#include <array>
#include <memory>
#include <string>

#include <glm/glm.hpp>

namespace dynamol
{
	class Protein;

	class Scene
	{
	public:
		Scene(const std::string &fileName);
		Protein* protein();
		const glm::vec3 &minimumBounds() const;
		const glm::vec3 &maximumBounds() const;

	public:
        static constexpr std::array<int32_t, 3> WorkGroupSize{8, 8, 8};

	private:
		glm::vec3 m_minimumBounds;
		glm::vec3 m_maximumBounds;
		std::unique_ptr<Protein> m_protein;
	};


}