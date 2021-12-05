#include "Scene.h"
#include "Protein.h"
#include <iostream>

using namespace dynamol;

Scene::Scene(const std::string &fileName)
{
	m_protein = std::make_unique<Protein>(fileName);
	m_minimumBounds = glm::floor(m_protein->minimumBounds());

	glm::ivec3 cubeSize{glm::ceil(m_protein->maximumBounds() + 1.0f - m_minimumBounds)};
	cubeSize.x = (cubeSize.x / WorkGroupSize[0] + 1) * WorkGroupSize[0];
	cubeSize.y = (cubeSize.y / WorkGroupSize[1] + 1) * WorkGroupSize[1];
	cubeSize.z = (cubeSize.z / WorkGroupSize[2] + 1) * WorkGroupSize[2];

	m_maximumBounds = m_minimumBounds + static_cast<glm::vec3>(cubeSize);
}

Protein * Scene::protein()
{
	return m_protein.get();
}

const glm::vec3 &Scene::minimumBounds() const
{
	return m_minimumBounds;
}

const glm::vec3 &Scene::maximumBounds() const
{
	return m_maximumBounds;
}