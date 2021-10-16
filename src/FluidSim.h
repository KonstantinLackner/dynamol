#pragma once

#include "Renderer.h"

#include <array>
#include <memory>

#include <glm/glm.hpp>
#include <glbinding/gl/gl.h>
#include <glbinding/gl/enum.h>
#include <glbinding/gl/functions.h>

#include <globjects/VertexArray.h>
#include <globjects/VertexAttributeBinding.h>
#include <globjects/Buffer.h>
#include <globjects/Program.h>
#include <globjects/Shader.h>
#include <globjects/Framebuffer.h>
#include <globjects/Renderbuffer.h>
#include <globjects/Texture.h>
#include <globjects/base/File.h>
#include <globjects/TextureHandle.h>
#include <globjects/NamedString.h>
#include <globjects/base/StaticStringSource.h>

class FluidSim
{
public:
	FluidSim(Renderer *renderer, const std::array<std::int32_t, 3> &windowDimensions, const std::array<std::int32_t, 3> &cubeDimensions);

private:
	Renderer *m_renderer;
	std::array<std::int32_t, 2> m_windowDimensions;
	std::array<std::int32_t, 3> m_cubeDimensions;
	std::unique_ptr<globjects::TransformFeedback> m_transformFeedback;
};