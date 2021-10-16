#include "FluidSim.h"

FluidSim::FluidSim(Renderer *const renderer, const std::array<std::int32_t, 3> &windowDimensions, const std::array<std::int32_t, 3> &cubeDimensions)
	: m_renderer{renderer},
	  m_windowDimensions{windowDimensions},
	  m_cubeDimensions{cubeDimensions}
{
	// shader loading here
}