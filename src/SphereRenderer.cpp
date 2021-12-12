#include "SphereRenderer.h"
#include <globjects/base/File.h>
#include <globjects/State.h>
#include <iostream>
#include <filesystem>
#include <imgui.h>
#include "Viewer.h"
#include "Scene.h"
#include "Protein.h"
#include <sstream>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

using namespace dynamol;
using namespace gl;
using namespace glm;
using namespace globjects;

void SphereRenderer::Molecule::addAtoms(const std::vector<glm::vec3> &atoms)
{
	std::vector<glm::vec4> transformedAtoms;
	transformedAtoms.reserve(atoms.size());
	std::transform(std::begin(atoms), std::end(atoms), std::back_inserter(transformedAtoms), [](const glm::vec3 &atom)
	{
		return glm::vec4{atom, 0.0f};
	});

	const auto addToBuffer = [&transformedAtoms, this](std::unique_ptr<Buffer> &buffer)
	{
		auto newBuffer = Buffer::create();
		newBuffer->setStorage((numberOfAtoms + transformedAtoms.size()) * sizeof(glm::vec4), nullptr, gl::GL_DYNAMIC_STORAGE_BIT);
		buffer->copySubData(newBuffer.get(), numberOfAtoms * sizeof(glm::vec4));
		newBuffer->setSubData(transformedAtoms, numberOfAtoms * sizeof(glm::vec4));

		std::swap(buffer, newBuffer);
	};

	addToBuffer(vertices.first);
	addToBuffer(vertices.second);
	++numberOfAtoms;
}

SphereRenderer::Molecule::Molecule(const std::vector<glm::vec4> &atoms)
	: vertices{Buffer::create(), Buffer::create()},
	  numberOfAtoms{atoms.size()}
{
	vertices.first->setStorage(atoms, gl::GL_NONE_BIT);
	vertices.second->setStorage(atoms.size() * sizeof(glm::vec4), nullptr, gl::GL_NONE_BIT);
}

std::unique_ptr<Texture> loadTexture(const std::string& filename)
{
	int width, height, channels;

	stbi_set_flip_vertically_on_load(true);
	unsigned char* data = stbi_load(filename.c_str(), &width, &height, &channels, 0);

	if (data)
	{
		std::cout << "Loaded " << filename << std::endl;

		auto texture = Texture::create(GL_TEXTURE_2D);
		texture->setParameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		texture->setParameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		texture->setParameter(GL_TEXTURE_WRAP_S, GL_REPEAT);
		texture->setParameter(GL_TEXTURE_WRAP_T, GL_REPEAT);

		GLenum format = GL_RGBA;

		switch (channels)
		{
		case 1:
			format = GL_RED;
			break;

		case 2:
			format = GL_RG;
			break;

		case 3:
			format = GL_RGB;
			break;

		case 4:
			format = GL_RGBA;
			break;
		}

		texture->image2D(0, format, ivec2(width, height), 0, format, GL_UNSIGNED_BYTE, data);
		texture->generateMipmap();

		stbi_image_free(data);

		return texture;
	}

	return std::unique_ptr<Texture>();
}

SphereRenderer::SphereRenderer(Viewer* viewer) : Renderer(viewer)
{
	Shader::hintIncludeImplementation(Shader::IncludeImplementation::Fallback);

	for (const auto &i : viewer->scene()->protein()->atoms())
	{
		m_molecules.emplace_back(i);
	}

	m_elementColorsRadii->setStorage(viewer->scene()->protein()->activeElementColorsRadiiPacked(), gl::GL_NONE_BIT);
	m_residueColors->setStorage(viewer->scene()->protein()->activeResidueColorsPacked(), gl::GL_NONE_BIT);
	m_chainColors->setStorage(viewer->scene()->protein()->activeChainColorsPacked(), gl::GL_NONE_BIT);

	m_intersectionBuffer->setStorage(sizeof(vec3) * 1024 * 1024 * 128 + sizeof(uint), nullptr, gl::GL_NONE_BIT);

	m_verticesQuad->setStorage(std::array<vec3, 1>({ vec3(0.0f, 0.0f, 0.0f) }), gl::GL_NONE_BIT);
	auto vertexBindingQuad = m_vaoQuad->binding(0);
	vertexBindingQuad->setBuffer(m_verticesQuad.get(), 0, sizeof(vec3));
	vertexBindingQuad->setFormat(3, GL_FLOAT);
	m_vaoQuad->enable(0);
	m_vaoQuad->unbind();

	m_shaderSourceDefines = StaticStringSource::create("");
	m_shaderDefines = NamedString::create("/defines.glsl", m_shaderSourceDefines.get());

	createShaderProgram("sphere", {
			{ GL_VERTEX_SHADER,"./res/sphere/sphere-vs.glsl" },
			{ GL_GEOMETRY_SHADER,"./res/sphere/sphere-gs.glsl" },
			{ GL_FRAGMENT_SHADER,"./res/sphere/sphere-fs.glsl" },
		},
		{ "./res/model/globals.glsl" });

	createShaderProgram("spawn", {
			{ GL_VERTEX_SHADER,"./res/sphere/sphere-vs.glsl" },
			{ GL_GEOMETRY_SHADER,"./res/sphere/sphere-gs.glsl" },
			{ GL_FRAGMENT_SHADER,"./res/sphere/spawn-fs.glsl" },
		},
		{ "./res/sphere/globals.glsl" });

	createShaderProgram("surface", {
			{ GL_VERTEX_SHADER,"./res/sphere/image-vs.glsl" },
			{ GL_GEOMETRY_SHADER,"./res/sphere/image-gs.glsl" },
			{ GL_FRAGMENT_SHADER,"./res/sphere/surface-fs.glsl" },
		},
		{ "./res/sphere/globals.glsl" });

	createShaderProgram("aosample", {
			{ GL_VERTEX_SHADER,"./res/sphere/image-vs.glsl" },
			{ GL_GEOMETRY_SHADER,"./res/sphere/image-gs.glsl" },
			{ GL_FRAGMENT_SHADER,"./res/sphere/aosample-fs.glsl" },
		},
		{ "./res/sphere/globals.glsl" });

	createShaderProgram("aoblur", {
			{ GL_VERTEX_SHADER,"./res/sphere/image-vs.glsl" },
			{ GL_GEOMETRY_SHADER,"./res/sphere/image-gs.glsl" },
			{ GL_FRAGMENT_SHADER,"./res/sphere/aoblur-fs.glsl" },
		},
		{ "./res/sphere/globals.glsl" });

	createShaderProgram("shade", {
			{ GL_VERTEX_SHADER,"./res/sphere/image-vs.glsl" },
			{ GL_GEOMETRY_SHADER,"./res/sphere/image-gs.glsl" },
			{ GL_FRAGMENT_SHADER,"./res/sphere/shade-fs.glsl" },
		},
		{ "./res/sphere/globals.glsl" });

	createShaderProgram("dofblur", {
			{ GL_VERTEX_SHADER,"./res/sphere/image-vs.glsl" },
			{ GL_GEOMETRY_SHADER,"./res/sphere/image-gs.glsl" },
			{ GL_FRAGMENT_SHADER,"./res/sphere/dofblur-fs.glsl" },
		},
		{ "./res/sphere/globals.glsl" });

	createShaderProgram("dofblend", {
			{ GL_VERTEX_SHADER,"./res/sphere/image-vs.glsl" },
			{ GL_GEOMETRY_SHADER,"./res/sphere/image-gs.glsl" },
			{ GL_FRAGMENT_SHADER,"./res/sphere/dofblend-fs.glsl" },
		},
		{ "./res/sphere/globals.glsl" });

	createShaderProgram("display", {
			{ GL_VERTEX_SHADER,"./res/sphere/image-vs.glsl" },
			{ GL_GEOMETRY_SHADER,"./res/sphere/image-gs.glsl" },
			{ GL_FRAGMENT_SHADER,"./res/sphere/display-fs.glsl" },
		},
		{ "./res/sphere/globals.glsl" });

	createShaderProgram("shadow", {
			{ GL_VERTEX_SHADER,"./res/sphere/sphere-vs.glsl" },
			{ GL_GEOMETRY_SHADER,"./res/sphere/sphere-gs.glsl" },
			{ GL_FRAGMENT_SHADER,"./res/sphere/shadow-fs.glsl" },
		},
		{ "./res/model/globals.glsl" });

	createShaderProgram("transformfeedback", {
			{ GL_VERTEX_SHADER, "./fluidsim/shader/transform_feedback.vert"},
			//{ GL_GEOMETRY_SHADER, "./fluidsim/shader/transform_feedback.geom"}
		},
		{ "./res/model/globals.glsl" });

	createShaderProgram("current_input_position", {
			{ GL_VERTEX_SHADER, "./fluidsim/shader/current_input_position.vert" },
			{ GL_FRAGMENT_SHADER, "./fluidsim/shader/current_input_position.frag" },
		},
		{ "./res/model/globals.glsl"});

	const glm::vec3 &minimumBounds{viewer->scene()->minimumBounds()};
	const glm::vec3 &maximumBounds{viewer->scene()->maximumBounds()};

	shaderProgram("transformfeedback")->setUniform("minBounds", minimumBounds);
	shaderProgram("transformfeedback")->setUniform("maxBounds", maximumBounds);

	m_framebufferSize = viewer->viewportSize();

	m_depthTexture = Texture::create(GL_TEXTURE_2D);
	m_depthTexture->setParameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	m_depthTexture->setParameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	m_depthTexture->setParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	m_depthTexture->setParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	m_depthTexture->image2D(0, GL_DEPTH_COMPONENT, m_framebufferSize, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_BYTE, nullptr);

	m_spherePositionTexture = Texture::create(GL_TEXTURE_2D);
	m_spherePositionTexture->setParameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	m_spherePositionTexture->setParameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	m_spherePositionTexture->setParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	m_spherePositionTexture->setParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	m_spherePositionTexture->image2D(0, GL_RGBA32F, m_framebufferSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

	m_sphereNormalTexture = Texture::create(GL_TEXTURE_2D);
	m_sphereNormalTexture->setParameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	m_sphereNormalTexture->setParameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	m_sphereNormalTexture->setParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	m_sphereNormalTexture->setParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	m_sphereNormalTexture->image2D(0, GL_RGBA32F, m_framebufferSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

	m_surfacePositionTexture = Texture::create(GL_TEXTURE_2D);
	m_surfacePositionTexture->setParameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	m_surfacePositionTexture->setParameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	m_surfacePositionTexture->setParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	m_surfacePositionTexture->setParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	m_surfacePositionTexture->image2D(0, GL_RGBA32F, m_framebufferSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

	m_surfaceNormalTexture = Texture::create(GL_TEXTURE_2D);
	m_surfaceNormalTexture->setParameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	m_surfaceNormalTexture->setParameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	m_surfaceNormalTexture->setParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	m_surfaceNormalTexture->setParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	m_surfaceNormalTexture->image2D(0, GL_RGBA32F, m_framebufferSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

	m_sphereDiffuseTexture = Texture::create(GL_TEXTURE_2D);
	m_sphereDiffuseTexture->setParameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	m_sphereDiffuseTexture->setParameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	m_sphereDiffuseTexture->setParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	m_sphereDiffuseTexture->setParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	m_sphereDiffuseTexture->image2D(0, GL_RGBA32F, m_framebufferSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

	m_surfaceDiffuseTexture = Texture::create(GL_TEXTURE_2D);
	m_surfaceDiffuseTexture->setParameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	m_surfaceDiffuseTexture->setParameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	m_surfaceDiffuseTexture->setParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	m_surfaceDiffuseTexture->setParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	m_surfaceDiffuseTexture->image2D(0, GL_RGBA32F, m_framebufferSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

	m_ambientTexture = Texture::create(GL_TEXTURE_2D);
	m_ambientTexture->setParameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	m_ambientTexture->setParameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	m_ambientTexture->setParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	m_ambientTexture->setParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	m_ambientTexture->image2D(0, GL_RGBA32F, m_framebufferSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

	m_blurTexture = Texture::create(GL_TEXTURE_2D);
	m_blurTexture->setParameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	m_blurTexture->setParameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	m_blurTexture->setParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	m_blurTexture->setParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	m_blurTexture->image2D(0, GL_RGBA32F, m_framebufferSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

	m_colorTexture = Texture::create(GL_TEXTURE_2D);
	m_colorTexture->setParameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	m_colorTexture->setParameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	m_colorTexture->setParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	m_colorTexture->setParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	m_colorTexture->image2D(0, GL_RGBA32F, m_framebufferSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

	m_offsetTexture = Texture::create(GL_TEXTURE_2D);
	m_offsetTexture->setParameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	m_offsetTexture->setParameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	m_offsetTexture->setParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	m_offsetTexture->setParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	m_offsetTexture->image2D(0, GL_R32UI, m_framebufferSize, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, nullptr);

	m_shadowColorTexture = Texture::create(GL_TEXTURE_2D);
	m_shadowColorTexture->setParameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	m_shadowColorTexture->setParameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	m_shadowColorTexture->setParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	m_shadowColorTexture->setParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	m_shadowColorTexture->setParameter(GL_TEXTURE_BORDER_COLOR, vec4(0.0, 0.0, 0.0, 65535.0f));
	m_shadowColorTexture->image2D(0, GL_RGBA32F, m_shadowMapSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

	m_shadowDepthTexture = Texture::create(GL_TEXTURE_2D);
	m_shadowDepthTexture->setParameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	m_shadowDepthTexture->setParameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	m_shadowDepthTexture->setParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	m_shadowDepthTexture->setParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	m_shadowDepthTexture->setParameter(GL_TEXTURE_BORDER_COLOR, vec4(0.0, 0.0, 0.0, 0.0));
	m_shadowDepthTexture->image2D(0, GL_DEPTH_COMPONENT, m_shadowMapSize, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_BYTE, nullptr);


	stbi_set_flip_vertically_on_load(true);

	for (auto& d : std::filesystem::directory_iterator("./dat/environments"))
	{
		std::filesystem::path environmentPath(d);

		std::unique_ptr<Texture> texture = loadTexture(environmentPath.string());

		if (texture)
			m_environmentTextures.push_back(std::move(texture));

	}

	for (auto& d : std::filesystem::directory_iterator("./dat/materials"))
	{
		std::filesystem::path materialPath(d);

		std::unique_ptr<Texture> texture = loadTexture(materialPath.string());

		if (texture)
		{
			texture->setParameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			texture->setParameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			texture->setParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			texture->setParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

			m_materialTextures.push_back(std::move(texture));
		}
	}

	for (auto& d : std::filesystem::directory_iterator("./dat/bumps"))
	{
		std::filesystem::path bumpPath(d);

		std::unique_ptr<Texture> texture = loadTexture(bumpPath.string());

		if (texture)
			m_bumpTextures.push_back(std::move(texture));
	}

	m_sphereFramebuffer = Framebuffer::create();
	m_sphereFramebuffer->attachTexture(GL_COLOR_ATTACHMENT0, m_spherePositionTexture.get());
	m_sphereFramebuffer->attachTexture(GL_COLOR_ATTACHMENT1, m_sphereNormalTexture.get());
	m_sphereFramebuffer->attachTexture(GL_DEPTH_ATTACHMENT, m_depthTexture.get());
	m_sphereFramebuffer->setDrawBuffers({ GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 });

	m_surfaceFramebuffer = Framebuffer::create();
	m_surfaceFramebuffer->attachTexture(GL_COLOR_ATTACHMENT0, m_surfacePositionTexture.get());
	m_surfaceFramebuffer->attachTexture(GL_COLOR_ATTACHMENT1, m_surfaceNormalTexture.get());
	m_surfaceFramebuffer->attachTexture(GL_COLOR_ATTACHMENT2, m_surfaceDiffuseTexture.get());
	m_surfaceFramebuffer->attachTexture(GL_COLOR_ATTACHMENT3, m_sphereDiffuseTexture.get());
	m_surfaceFramebuffer->attachTexture(GL_DEPTH_ATTACHMENT, m_depthTexture.get());
	m_surfaceFramebuffer->setDrawBuffers({ GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3 });

	m_shadeFramebuffer = Framebuffer::create();
	m_shadeFramebuffer->attachTexture(GL_COLOR_ATTACHMENT0, m_colorTexture.get());
	m_shadeFramebuffer->setDrawBuffers({ GL_COLOR_ATTACHMENT0 });
	m_shadeFramebuffer->attachTexture(GL_DEPTH_ATTACHMENT, m_depthTexture.get());

	m_aoBlurFramebuffer = Framebuffer::create();
	m_aoBlurFramebuffer->attachTexture(GL_COLOR_ATTACHMENT0, m_blurTexture.get());
	m_aoBlurFramebuffer->setDrawBuffers({ GL_COLOR_ATTACHMENT0 });

	m_aoFramebuffer = Framebuffer::create();
	m_aoFramebuffer->attachTexture(GL_COLOR_ATTACHMENT0, m_ambientTexture.get());
	m_aoFramebuffer->setDrawBuffers({ GL_COLOR_ATTACHMENT0 });

	m_dofBlurFramebuffer = Framebuffer::create();
	m_dofBlurFramebuffer->attachTexture(GL_COLOR_ATTACHMENT0, m_sphereNormalTexture.get());
	m_dofBlurFramebuffer->attachTexture(GL_COLOR_ATTACHMENT1, m_surfaceNormalTexture.get());
	m_dofBlurFramebuffer->setDrawBuffers({ GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 });

	m_dofFramebuffer = Framebuffer::create();
	m_dofFramebuffer->attachTexture(GL_COLOR_ATTACHMENT0, m_sphereDiffuseTexture.get());
	m_dofFramebuffer->attachTexture(GL_COLOR_ATTACHMENT1, m_surfaceDiffuseTexture.get());
	m_dofFramebuffer->setDrawBuffers({ GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 });

	m_shadowFramebuffer = Framebuffer::create();
	m_shadowFramebuffer->attachTexture(GL_COLOR_ATTACHMENT0, m_shadowColorTexture.get());
	m_shadowFramebuffer->attachTexture(GL_DEPTH_ATTACHMENT, m_shadowDepthTexture.get());
	m_shadowFramebuffer->setDrawBuffers({ GL_COLOR_ATTACHMENT0 });

	m_transformFeedback.reset(new TransformFeedback());
	m_transformFeedback->setVaryings(shaderProgram("transformfeedback"), {"outCoords"}, GL_INTERLEAVED_ATTRIBS);


	for (auto &inputPosition : m_inputPositions)
	{
		inputPosition.enabled = true;
		inputPosition.currentInputPosition = (minimumBounds + maximumBounds) / 2.0f;
		inputPosition.currentInputPositionIndicatorBuffer = Buffer::create();
		inputPosition.currentInputPositionIndicatorBuffer->setStorage(sizeof(glm::vec3), glm::value_ptr(inputPosition.currentInputPosition), gl::GL_DYNAMIC_STORAGE_BIT);

		auto vertexBindingInput = inputPosition.currentInputPositionVao->binding(0);
		vertexBindingInput->setAttribute(0);
		vertexBindingInput->setBuffer(inputPosition.currentInputPositionIndicatorBuffer.get(), 0, sizeof(glm::vec3));
		vertexBindingInput->setFormat(3, GL_FLOAT);
		inputPosition.currentInputPositionVao->enable(0);
	}
}

void SphereRenderer::display()
{
	if (viewer()->scene()->protein()->atoms().size() == 0)
		return;

	// SaveOpenGL state
	auto currentState = State::currentState();

	const ivec2 viewportSize = ivec2(vec2(viewer()->viewportSize()) * m_resolutionScale);

	// Resize all FBOs if the viewport size has changed
	if (viewportSize != m_framebufferSize)
	{
		m_framebufferSize = viewportSize;
		m_offsetTexture->image2D(0, GL_R32UI, m_framebufferSize, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, nullptr);
		m_depthTexture->image2D(0, GL_DEPTH_COMPONENT, m_framebufferSize, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_BYTE, nullptr);
		m_spherePositionTexture->image2D(0, GL_RGBA32F, m_framebufferSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		m_sphereNormalTexture->image2D(0, GL_RGBA32F, m_framebufferSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		m_surfacePositionTexture->image2D(0, GL_RGBA32F, m_framebufferSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		m_surfaceNormalTexture->image2D(0, GL_RGBA32F, m_framebufferSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		m_surfaceDiffuseTexture->image2D(0, GL_RGBA32F, m_framebufferSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		m_sphereDiffuseTexture->image2D(0, GL_RGBA32F, m_framebufferSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		m_ambientTexture->image2D(0, GL_RGBA32F, m_framebufferSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		m_blurTexture->image2D(0, GL_RGBA32F, m_framebufferSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		m_colorTexture->image2D(0, GL_RGBA32F, m_framebufferSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	}

	// our shader programs

	auto programSphere = shaderProgram("sphere");
	auto programSpawn = shaderProgram("spawn");
	auto programSurface = shaderProgram("surface");
	auto programAOSample = shaderProgram("aosample");
	auto programAOBlur = shaderProgram("aoblur");
	auto programShade = shaderProgram("shade");
	auto programDOFBlur = shaderProgram("dofblur");
	auto programDOFBlend = shaderProgram("dofblend");
	auto programDisplay = shaderProgram("display");
	auto programShadow = shaderProgram("shadow");
	auto programTransformFeedback = shaderProgram("transformfeedback");
	auto programCurrentInputPosition = shaderProgram("current_input_position");

	// get cursor position for magic lens
	double mouseX, mouseY;
	glfwGetCursorPos(viewer()->window(), &mouseX, &mouseY);
	const vec2 focusPosition = vec2(2.0f*float(mouseX) / float(viewportSize.x) - 1.0f, -2.0f*float(mouseY) / float(viewportSize.y) + 1.0f);

	// retrieve/compute all necessary matrices and related properties
	const mat4 viewMatrix = viewer()->viewTransform();
	const mat4 inverseViewMatrix = inverse(viewMatrix);
	const mat4 modelViewMatrix = viewer()->modelViewTransform();
	const mat4 inverseModelViewMatrix = inverse(modelViewMatrix);
	const mat4 modelLightMatrix = viewer()->modelLightTransform();
	const mat4 inverseModelLightMatrix = inverse(modelLightMatrix);
	const mat4 modelViewProjectionMatrix = viewer()->modelViewProjectionTransform();
	const mat4 inverseModelViewProjectionMatrix = inverse(modelViewProjectionMatrix);
	const mat4 modelLightProjectionMatrix = viewer()->modelLightProjectionTransform();
	const mat4 inverseModelLightProjectionMatrix = inverse(modelLightProjectionMatrix);
	const mat4 projectionMatrix = viewer()->projectionTransform();
	const mat4 inverseProjectionMatrix = inverse(projectionMatrix);
	const mat3 normalMatrix = mat3(transpose(inverseModelViewMatrix));
	const mat3 inverseNormalMatrix = inverse(normalMatrix);

	const glm::vec3 &minimumBounds{viewer()->scene()->minimumBounds()};
	const glm::vec3 &maximumBounds{viewer()->scene()->maximumBounds()};

	const vec3 objectCenter = 0.5f * (maximumBounds + minimumBounds);
	const float objectRadius = 0.5f * length(maximumBounds - minimumBounds);

	const vec4 projectionInfo(float(-2.0 / (viewportSize.x * projectionMatrix[0][0])),
		float(-2.0 / (viewportSize.y * projectionMatrix[1][1])),
		float((1.0 - (double)projectionMatrix[0][2]) / projectionMatrix[0][0]),
		float((1.0 + (double)projectionMatrix[1][2]) / projectionMatrix[1][1]));

	const float projectionScale = float(viewportSize.y) / fabs(2.0f / projectionMatrix[1][1]);
	const float fieldOfView = 2.0f * atan(1.0f / projectionMatrix[1][1]);

	vec4 nearPlane = inverseProjectionMatrix * vec4(0.0, 0.0, -1.0, 1.0);
	nearPlane /= nearPlane.w;

	vec4 worldLightPosition = inverseModelLightMatrix * vec4(0.0f, 0.0f, 0.0f, 1.0f);
	vec4 viewLightPosition = modelViewMatrix * worldLightPosition;

	// all input parameters and their default values
	static vec3 ambientMaterial = viewer()->backgroundColor();// (0.3f, 0.3f, 0.3f);
	static vec3 diffuseMaterial(0.6f, 0.6f, 0.6f);
	static vec3 specularMaterial(0.3f, 0.3f, 0.3f);
	static float shininess = 20.0f;
	static float sharpness = 1.0f;

	static float distanceBlending = 0.0f;
	static float distanceScale = 1.0;

	static bool ambientOcclusion = false;
	static bool environmentMapping = false;
	static bool environmentLighting = false;
	static bool normalMapping = false;
	static bool materialMapping = false;
	static bool depthOfField = false;

	static int coloring = 0;
	static bool animate = false;
	static float animationAmplitude = 1.0f;
	static float animationFrequency = 1.0f;
	static bool lens = false;

	static float focalDistance = 2.0f * sqrt(3.0f);
	static float maximumCoCRadius = 9.0f;
	static float farRadiusRescale = 1.0f;
	static float focalLength = 1.0f;
	static float aparture = 1.0f;
	static float fStop = 1.0;
	static int fStop_current = 12;
	const char* fStops[] = { "0.7", "0.8", "1.0", "1.2", "1.4", "1.7", "2.0", "2.4", "2.8", "3.3", "4.0", "4.8", "5.6", "6.7", "8.0", "9.5", "11.0", "16.0", "22.0", "32.0" };

	static uint environmentTextureIndex = 0;
	static uint materialTextureIndex = 0;
	static uint bumpTextureIndex = 0;

	// user interface for manipulating rendering parameters
	if (ImGui::BeginMenu("Renderer"))
	{
		ImGui::SliderFloat("Resolution Scale", &m_resolutionScale, 0.25f, 8.0f);

		if (ImGui::CollapsingHeader("Lighting"))
		{
			ImGui::ColorEdit3("Ambient", (float*)&ambientMaterial);
			ImGui::ColorEdit3("Diffuse", (float*)&diffuseMaterial);
			ImGui::ColorEdit3("Specular", (float*)&specularMaterial);
			ImGui::SliderFloat("Shininess", &shininess, 1.0f, 256.0f);
			ImGui::Checkbox("Ambient Occlusion Enabled", &ambientOcclusion);
			ImGui::Checkbox("Material Mapping Enabled", &materialMapping);
			ImGui::Checkbox("Normal Mapping Enabled", &normalMapping);
			ImGui::Checkbox("Environment Mapping Enabled", &environmentMapping);
			ImGui::Checkbox("Depth of Field Enabled", &depthOfField);
		}

		if (ImGui::CollapsingHeader("Surface"))
		{
			ImGui::SliderFloat("Sharpness", &sharpness, 0.5f, 16.0f);
			ImGui::SliderFloat("Dist. Blending", &distanceBlending, 0.0f, 1.0f);
			ImGui::SliderFloat("Dist. Scale", &distanceScale, 0.0f, 16.0f);
			ImGui::Combo("Coloring", &coloring, "None\0Element\0Residue\0Chain\0");
			ImGui::Checkbox("Magic Lens", &lens);
		}


		if (environmentMapping)
		{
			if (ImGui::CollapsingHeader("Environment Mapping"))
			{
				if (ImGui::ListBoxHeader("Environment Map"))
				{
					for (uint i = 0; i < m_environmentTextures.size(); i++)
					{
						auto& texture = m_environmentTextures[i];
						bool selected = (i == environmentTextureIndex);
						ImGui::BeginGroup();
						ImGui::PushID(i);

						if (ImGui::Selectable("", &selected, 0, ImVec2(0.0f, 32.0f)))
							environmentTextureIndex = i;

						ImGui::SameLine();
						ImGui::Image((ImTextureID)texture->id(), ImVec2(32.0f, 32.0f));
						ImGui::PopID();
						ImGui::EndGroup();
					}

					ImGui::ListBoxFooter();
				}

				ImGui::Checkbox("Use for Illumination", &environmentLighting);
			}
		}


		if (materialMapping)
		{
			if (ImGui::CollapsingHeader("Material Mapping"))
			{
				if (ImGui::ListBoxHeader("Material Map"))
				{
					for (uint i = 0; i < m_materialTextures.size(); i++)
					{
						auto& texture = m_materialTextures[i];
						bool selected = (i == materialTextureIndex);
						ImGui::BeginGroup();
						ImGui::PushID(i);

						if (ImGui::Selectable("", &selected, 0, ImVec2(0.0f, 32.0f)))
							materialTextureIndex = i;

						ImGui::SameLine();
						ImGui::Image((ImTextureID)texture->id(), ImVec2(32.0f, 32.0f));
						ImGui::PopID();
						ImGui::EndGroup();
					}

					ImGui::ListBoxFooter();
				}
			}
		}


		if (normalMapping)
		{
			if (ImGui::CollapsingHeader("Normal Mapping"))
			{
				if (ImGui::ListBoxHeader("Normal Map"))
				{
					for (uint i = 0; i < m_bumpTextures.size(); i++)
					{
						auto& texture = m_bumpTextures[i];
						bool selected = (i == bumpTextureIndex);
						ImGui::BeginGroup();
						ImGui::PushID(i);

						if (ImGui::Selectable("", &selected, 0, ImVec2(0.0f, 32.0f)))
							bumpTextureIndex = i;

						ImGui::SameLine();
						ImGui::Image((ImTextureID)texture->id(), ImVec2(32.0f, 32.0f));
						ImGui::PopID();
						ImGui::EndGroup();
					}

					ImGui::ListBoxFooter();
				}
			}
		}

		if (depthOfField)
		{
			if (ImGui::CollapsingHeader("Depth of Field"))
			{
				ImGui::SliderFloat("Focal Distance", &focalDistance, 0.1f, 35.0f);
				ImGui::Combo("F-stop", &fStop_current, fStops, IM_ARRAYSIZE(fStops));

				ImGui::SliderFloat("Max. CoC Radius", &maximumCoCRadius, 1.0f, 20.0f);
				ImGui::SliderFloat("Far Radius Scale", &farRadiusRescale, 0.1f, 5.0f);

			}
		}

		fStop = std::stof(fStops[fStop_current]);
		focalLength = 1.0f / (tan(fieldOfView * 0.5f) * 2.0f);
		aparture = focalLength / fStop;

		if (ImGui::CollapsingHeader("Animation"))
		{
			ImGui::Checkbox("Prodecural Animation", &animate);
			ImGui::SliderFloat("Frequency", &animationFrequency, 1.0f, 256.0f);
			ImGui::SliderFloat("Amplitude", &animationAmplitude, 1.0f, 32.0f);
		}

		if (ImGui::CollapsingHeader("Input"))
		{
			const bool disabled{running};
			if (disabled)
			{
				ImGui::BeginDisabled();
			}

			bool changed{false};
			const auto slider = [&changed, &minimumBounds, &maximumBounds, this](const char *const name, float glm::vec3::*axis, const InputMode inputMode)
			{
				changed |= ImGui::SliderFloat(name, &(m_inputPositions[inputMode].currentInputPosition.*axis), minimumBounds.*axis, maximumBounds.*axis, "%.3f", ImGuiSliderFlags_AlwaysClamp);
			};

			slider("X", &glm::vec3::x, IM_Atom);
			slider("Y", &glm::vec3::y, IM_Atom);
			slider("Z", &glm::vec3::z, IM_Atom);

			if (changed)
			{
				m_inputPositions[IM_Atom].updateCurrentInputPositionBuffer();
			}

			ImGui::RadioButton("Atoms", &m_inputMode, IM_Atom);
			ImGui::RadioButton("Force", &m_inputMode, IM_Force);

			m_inputPositions[IM_Force].enabled = m_inputMode == IM_Force;
			auto &inputPosition = m_inputPositions[m_inputMode];

			switch (m_inputMode)
			{
			case IM_Atom:
				ImGui::SliderInt("Amount", &m_numberOfAtomsToAdd, 1, 100, "%d", ImGuiSliderFlags_AlwaysClamp);

				if (ImGui::Button("Add Atom"))
				{
					std::vector<glm::vec3> atoms;
					atoms.resize(static_cast<std::size_t>(m_numberOfAtomsToAdd), inputPosition.currentInputPosition);

					for (auto &molecule : m_molecules)
					{
						molecule.addAtoms(atoms);
					}
				}

				break;

			case IM_Force:
				ImGui::Spacing();
				changed = false;
				slider("X2", &glm::vec3::x, IM_Force);
				slider("Y2", &glm::vec3::y, IM_Force);
				slider("Z2", &glm::vec3::z, IM_Force);
				ImGui::Spacing();

				if (changed)
				{
					m_inputPositions[IM_Force].updateCurrentInputPositionBuffer();
				}

				ImGui::SliderFloat("Force Multiplier", &m_currentInputForce, 0.1f, 5.0f);
				ImGui::SliderInt("Number Of Frames", &m_remainingFrames, 1, 100, "%d", ImGuiSliderFlags_AlwaysClamp);

				if (m_remainingFrames == 0)
				{
					ImGui::BeginDisabled();
				}

				if (ImGui::Button("Add Force"))
				{
					running = true;
					m_holdForFrames = m_remainingFrames;
				}

				if (m_remainingFrames == 0)
				{
					ImGui::EndDisabled();
				}

				break;
			}

			if (disabled)
			{
				ImGui::EndDisabled();
			}
		}

		ImGui::EndMenu();
	}

	if (running)
	{
		viewer()->fluidSim()->AddTwoPointImpulse({{m_inputPositions[0].currentInputPosition - minimumBounds, m_inputPositions[1].currentInputPosition - minimumBounds}, m_currentInputForce * (static_cast<float>(m_remainingFrames) / m_holdForFrames)});
		running = --m_remainingFrames > 0;
	}

	// Scaling for sphere of influence radius based on estimated density
	const float contributingAtoms = 32.0f;
	const float radiusScale = sqrtf(log(contributingAtoms * exp(sharpness)) / sharpness);

	// Properties for animation
	const uint timestepCount = (uint)m_molecules.size();
	const float animationTime = animate ? float(glfwGetTime()) : -1.0f;
	const float currentTime = glfwGetTime() * animationFrequency;
	const uint currentTimestep = uint(currentTime) % timestepCount;
	const uint nextTimestep = (currentTimestep + 1) % timestepCount;
	const float animationDelta = currentTime - floor(currentTime);
	auto &selectedMolecule{m_molecules[currentTimestep]};
	const int vertexCount = int(selectedMolecule.numberOfAtoms);

	// Defines for enabling/disabling shader feature based on parameter setting
	std::string defines = "";

	if (animate)
		defines += "#define ANIMATION\n";

	if (lens)
		defines += "#define LENSING\n";

	if (coloring > 0)
		defines += "#define COLORING\n";

	if (ambientOcclusion)
		defines += "#define AMBIENT\n";

	if (environmentMapping)
		defines += "#define ENVIRONMENT\n";

	if (environmentMapping && environmentLighting)
		defines += "#define ENVIRONMENTLIGHTING\n";

	if (normalMapping)
		defines += "#define NORMAL\n";

	if (materialMapping)
		defines += "#define MATERIAL\n";

	if (depthOfField)
		defines += "#define DEPTHOFFIELD\n";

	// Reload shaders if settings have changed
	if (defines != m_shaderSourceDefines->string())
	{
		m_shaderSourceDefines->setString(defines);
		reloadShaders();
	}

	// Read buffer from fluidsim
	// Vertex binding setup
	const globjects::Buffer *const buffer{selectedMolecule.vertices.first.get()};
	auto vertexBinding = m_vao->binding(0);
	vertexBinding->setAttribute(0);
	vertexBinding->setBuffer(buffer, 0, sizeof(vec4));
	vertexBinding->setFormat(4, GL_FLOAT);
	m_vao->enable(0);


	glEnable(GL_RASTERIZER_DISCARD);

	selectedMolecule.vertices.second->bindBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0);
	//viewer()->fluidSim()->GetVelocityTexture().BindImage(0, GL_READ_ONLY);
	viewer()->fluidSim()->GetVelocityTexture().Bind(0);
	programTransformFeedback->use();

	m_transformFeedback->bind();
	m_transformFeedback->begin(GL_POINTS);

	m_vao->drawArrays(GL_POINTS, 0, vertexCount);

	m_transformFeedback->end();
	//m_transformFeedback->unbind();
	glFlush();

	glDisable(GL_RASTERIZER_DISCARD);
	glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0);

	vertexBinding->setBuffer(selectedMolecule.vertices.second.get(), 0, sizeof(glm::vec4));

	std::swap(selectedMolecule.vertices.first, selectedMolecule.vertices.second);

	/* Used for interploation, which is not active
	if (timestepCount > 0)
	{
		auto nextVertexBinding = m_vao->binding(1);
		nextVertexBinding->setAttribute(1);
		nextVertexBinding->setBuffer(m_vertices[nextTimestep].get(), 0, sizeof(vec4));
		nextVertexBinding->setFormat(4, GL_FLOAT);
		m_vao->enable(1);
	}*/

	//////////////////////////////////////////////////////////////////////////
	// Shadow rendering pass
	//////////////////////////////////////////////////////////////////////////
	// experimental ground place shadow (disabled for now)
	/*
	glViewport(0, 0, m_shadowMapSize.x, m_shadowMapSize.y);

	m_shadowFramebuffer->bind();
	glClearDepth(1.0f);
	glClearColor(0.0, 0.0, 0.0, 65535.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE);
	glBlendEquation(GL_MAX);
	//glBlendEquation(GL_FUNC_ADD);

	glEnable(GL_DEPTH_TEST);
//	glDepthFunc(GL_LESS);
	glDepthFunc(GL_ALWAYS);

	programShadow->setUniform("modelViewMatrix", modelLightMatrix);
	programShadow->setUniform("projectionMatrix", projectionMatrix);
	programShadow->setUniform("modelViewProjectionMatrix", modelLightProjectionMatrix);
	programShadow->setUniform("inverseModelViewProjectionMatrix", inverseModelLightProjectionMatrix);
	programShadow->setUniform("radiusScale", 2.0f*radiusScale);
	programShadow->setUniform("clipRadiusScale", radiusScale);
	programShadow->setUniform("nearPlaneZ", nearPlane.z);
	programShadow->setUniform("animationDelta", animationDelta);
	programShadow->setUniform("animationTime", animationTime);
	programShadow->setUniform("animationAmplitude", animationAmplitude);
	programShadow->setUniform("animationFrequency", animationFrequency);

	m_vao->bind();
	programShadow->use();
	m_vao->drawArrays(GL_POINTS, 0, vertexCount);
	programShadow->release();
	m_vao->unbind();
	m_shadowFramebuffer->unbind();

	glDisable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);

	glViewport(0, 0, viewportSize.x, viewportSize.y);
	*/
	//////////////////////////////////////////////////////////////////////////
	// Sphere rendering pass
	//////////////////////////////////////////////////////////////////////////
	m_sphereFramebuffer->bind();
	glClearDepth(1.0f);
	glClearColor(0.0, 0.0, 0.0, 65535.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);

	programSphere->setUniform("modelViewMatrix", modelViewMatrix);
	programSphere->setUniform("projectionMatrix", projectionMatrix);
	programSphere->setUniform("modelViewProjectionMatrix", modelViewProjectionMatrix);
	programSphere->setUniform("inverseModelViewProjectionMatrix", inverseModelViewProjectionMatrix);
	programSphere->setUniform("radiusScale", 1.0f);
	programSphere->setUniform("clipRadiusScale", radiusScale);
	programSphere->setUniform("nearPlaneZ", nearPlane.z);
	programSphere->setUniform("animationDelta", animationDelta);
	programSphere->setUniform("animationTime", animationTime);
	programSphere->setUniform("animationAmplitude", animationAmplitude);
	programSphere->setUniform("animationFrequency", animationFrequency);

	m_vao->bind();
	programSphere->use();
	m_vao->drawArrays(GL_POINTS, 0, vertexCount);
	programSphere->release();
	m_vao->unbind();

	//////////////////////////////////////////////////////////////////////////
	// List generation pass
	//////////////////////////////////////////////////////////////////////////
	const uint intersectionClearValue = 1;
	m_intersectionBuffer->clearSubData(GL_R32UI, 0, sizeof(uint), GL_RED_INTEGER, GL_UNSIGNED_INT, &intersectionClearValue);

	const uint offsetClearValue = 0;
	m_offsetTexture->clearImage(0, GL_RED_INTEGER, GL_UNSIGNED_INT, &offsetClearValue);

	glMemoryBarrier(GL_ALL_BARRIER_BITS);

	glDepthFunc(GL_ALWAYS);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	glDepthMask(GL_FALSE);

	m_spherePositionTexture->bindActive(0);
	m_offsetTexture->bindImageTexture(0, 0, false, 0, GL_READ_WRITE, GL_R32UI);
	m_elementColorsRadii->bindBase(GL_UNIFORM_BUFFER, 0);
	m_residueColors->bindBase(GL_UNIFORM_BUFFER, 1);
	m_chainColors->bindBase(GL_UNIFORM_BUFFER, 2);

	programSpawn->setUniform("modelViewMatrix", modelViewMatrix);
	programSpawn->setUniform("projectionMatrix", projectionMatrix);
	programSpawn->setUniform("modelViewProjectionMatrix", modelViewProjectionMatrix);
	programSpawn->setUniform("inverseModelViewProjectionMatrix", inverseModelViewProjectionMatrix);
	programSpawn->setUniform("radiusScale", radiusScale);
	programSpawn->setUniform("clipRadiusScale", radiusScale);
	programSpawn->setUniform("nearPlaneZ", nearPlane.z);
	programSpawn->setUniform("animationDelta", animationDelta);
	programSpawn->setUniform("animationTime", animationTime);
	programSpawn->setUniform("animationAmplitude", animationAmplitude);
	programSpawn->setUniform("animationFrequency", animationFrequency);

	m_vao->bind();
	programSpawn->use();
	m_vao->drawArrays(GL_POINTS, 0, vertexCount);
	programSpawn->release();
	m_vao->unbind();


	m_spherePositionTexture->unbindActive(0);
	m_intersectionBuffer->unbind(GL_SHADER_STORAGE_BUFFER);
	m_offsetTexture->unbindImageTexture(0);

	m_sphereFramebuffer->unbind();
	glMemoryBarrier(GL_ALL_BARRIER_BITS);

	//////////////////////////////////////////////////////////////////////////
	// Surface intersection pass
	//////////////////////////////////////////////////////////////////////////
	m_surfaceFramebuffer->bind();
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthMask(GL_TRUE);

	glClearDepth(1.0f);
	glClearColor(0.0f, 0.0f, 0.0f, 65535.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	m_spherePositionTexture->bindActive(0);
	m_sphereNormalTexture->bindActive(1);
	m_offsetTexture->bindActive(3);
	m_environmentTextures[environmentTextureIndex]->bindActive(4);
	m_bumpTextures[bumpTextureIndex]->bindActive(5);
	m_materialTextures[materialTextureIndex]->bindActive(6);
	m_intersectionBuffer->bindBase(GL_SHADER_STORAGE_BUFFER, 1);
	m_statisticsBuffer->bindBase(GL_SHADER_STORAGE_BUFFER, 2);

	programSurface->setUniform("modelViewMatrix", modelViewMatrix);
	programSurface->setUniform("projectionMatrix", projectionMatrix);
	programSurface->setUniform("modelViewProjectionMatrix", modelViewProjectionMatrix);
	programSurface->setUniform("inverseModelViewProjectionMatrix", inverseModelViewProjectionMatrix);
	programSurface->setUniform("normalMatrix", normalMatrix);
	programSurface->setUniform("lightPosition", vec3(worldLightPosition));
	programSurface->setUniform("ambientMaterial", ambientMaterial);
	programSurface->setUniform("diffuseMaterial", diffuseMaterial);
	programSurface->setUniform("specularMaterial", specularMaterial);
	programSurface->setUniform("shininess", shininess);
	programSurface->setUniform("focusPosition", focusPosition);
	programSurface->setUniform("positionTexture", 0);
	programSurface->setUniform("normalTexture", 1);
	programSurface->setUniform("offsetTexture", 3);
	programSurface->setUniform("environmentTexture", 4);
	programSurface->setUniform("bumpTexture", 5);
	programSurface->setUniform("materialTexture", 6);
	programSurface->setUniform("sharpness", sharpness);
	programSurface->setUniform("coloring", uint(coloring));
	programSurface->setUniform("environment", environmentMapping);
	programSurface->setUniform("lens", lens);

	m_vaoQuad->bind();
	programSurface->use();
	m_vaoQuad->drawArrays(GL_POINTS, 0, 1);
	programSurface->release();
	m_vaoQuad->unbind();

	m_intersectionBuffer->unbind(GL_SHADER_STORAGE_BUFFER);

	m_materialTextures[materialTextureIndex]->unbindActive(6);
	m_bumpTextures[bumpTextureIndex]->unbindActive(5);
	m_environmentTextures[environmentTextureIndex]->unbindActive(4);
	m_offsetTexture->unbindActive(3);
	m_sphereNormalTexture->unbindActive(1);
	m_spherePositionTexture->unbindActive(0);

	m_chainColors->unbind(GL_UNIFORM_BUFFER);
	m_residueColors->unbind(GL_UNIFORM_BUFFER);
	m_elementColorsRadii->unbind(GL_UNIFORM_BUFFER);

	m_surfaceFramebuffer->unbind();


	//////////////////////////////////////////////////////////////////////////
	// Ambient occlusion (optional)
	//////////////////////////////////////////////////////////////////////////
	if (ambientOcclusion)
	{
		//////////////////////////////////////////////////////////////////////////
		// Ambient occlusion sampling
		//////////////////////////////////////////////////////////////////////////
		m_aoFramebuffer->bind();

		programAOSample->setUniform("projectionInfo", projectionInfo);
		programAOSample->setUniform("projectionScale", projectionScale);
		programAOSample->setUniform("viewLightPosition", viewLightPosition);
		programAOSample->setUniform("surfaceNormalTexture", 0);

		m_surfaceNormalTexture->bindActive(0);

		m_vaoQuad->bind();
		programAOSample->use();
		m_vaoQuad->drawArrays(GL_POINTS, 0, 1);
		programAOSample->release();
		m_vaoQuad->unbind();

		m_aoFramebuffer->unbind();


		//////////////////////////////////////////////////////////////////////////
		// Ambient occlusion blurring -- horizontal
		//////////////////////////////////////////////////////////////////////////
		m_aoBlurFramebuffer->bind();
		programAOBlur->setUniform("normalTexture", 0);
		programAOBlur->setUniform("ambientTexture", 1);
		programAOBlur->setUniform("offset", vec2(1.0f / float(viewportSize.x), 0.0f));

		m_ambientTexture->bindActive(1);

		m_vaoQuad->bind();
		programAOBlur->use();
		m_vaoQuad->drawArrays(GL_POINTS, 0, 1);
		programAOBlur->release();
		m_vaoQuad->unbind();

		m_ambientTexture->unbindActive(1);
		m_aoBlurFramebuffer->unbind();


		//////////////////////////////////////////////////////////////////////////
		// Ambient occlusion blurring -- vertical
		//////////////////////////////////////////////////////////////////////////
		m_aoFramebuffer->bind();
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		programAOBlur->setUniform("offset", vec2(0.0f, 1.0f / float(viewportSize.y)));

		m_blurTexture->bindActive(1);

		m_vaoQuad->bind();
		programAOBlur->use();
		m_vaoQuad->drawArrays(GL_POINTS, 0, 1);
		programAOBlur->release();
		m_vaoQuad->unbind();

		m_blurTexture->unbindActive(1);
		m_surfaceNormalTexture->unbindActive(0);

		m_aoFramebuffer->unbind();
	}

	//////////////////////////////////////////////////////////////////////////
	// Shading
	//////////////////////////////////////////////////////////////////////////
	m_shadeFramebuffer->bind();
	glDepthMask(GL_FALSE);

	m_spherePositionTexture->bindActive(0);
	m_sphereNormalTexture->bindActive(1);
	m_sphereDiffuseTexture->bindActive(2);
	m_surfacePositionTexture->bindActive(3);
	m_surfaceNormalTexture->bindActive(4);
	m_surfaceDiffuseTexture->bindActive(5);
	m_depthTexture->bindActive(6);
	m_ambientTexture->bindActive(7);
	m_materialTextures[materialTextureIndex]->bindActive(8);
	m_environmentTextures[environmentTextureIndex]->bindActive(9);
	m_shadowColorTexture->bindActive(10);
	m_shadowDepthTexture->bindActive(11);

	programShade->setUniform("modelViewMatrix", modelViewMatrix);
	programShade->setUniform("projectionMatrix", projectionMatrix);
	programShade->setUniform("modelViewProjection", modelViewProjectionMatrix);
	programShade->setUniform("inverseModelViewProjectionMatrix", inverseModelViewProjectionMatrix);
	programShade->setUniform("normalMatrix", normalMatrix);
	programShade->setUniform("inverseNormalMatrix", inverseNormalMatrix);
	programShade->setUniform("modelLightMatrix", modelLightMatrix);
	programShade->setUniform("modelLightProjectionMatrix", modelLightProjectionMatrix);
	programShade->setUniform("lightPosition", vec3(worldLightPosition));
	programShade->setUniform("ambientMaterial", ambientMaterial);
	programShade->setUniform("diffuseMaterial", diffuseMaterial);
	programShade->setUniform("specularMaterial", specularMaterial);
	programShade->setUniform("distanceBlending", distanceBlending);
	programShade->setUniform("distanceScale", distanceScale);
	programShade->setUniform("shininess", shininess);
	programShade->setUniform("focusPosition", focusPosition);
	programShade->setUniform("objectCenter", objectCenter);
	programShade->setUniform("objectRadius", objectRadius);

	programShade->setUniform("spherePositionTexture", 0);
	programShade->setUniform("sphereNormalTexture", 1);
	programShade->setUniform("sphereDiffuseTexture", 2);

	programShade->setUniform("surfacePositionTexture", 3);
	programShade->setUniform("surfaceNormalTexture", 4);
	programShade->setUniform("surfaceDiffuseTexture", 5);

	programShade->setUniform("depthTexture", 6);
	programShade->setUniform("ambientTexture", 7);
	programShade->setUniform("materialTexture", 8);
	programShade->setUniform("environmentTexture", 9);
	programShade->setUniform("shadowColorTexture", 10);
	programShade->setUniform("shadowDepthTexture", 11);

	programShade->setUniform("environment", environmentMapping);
	programShade->setUniform("maximumCoCRadius", maximumCoCRadius);
	programShade->setUniform("aparture", aparture);
	programShade->setUniform("focalDistance", focalDistance);
	programShade->setUniform("focalLength", focalLength);
	programShade->setUniform("backgroundColor", viewer()->backgroundColor());


	m_vaoQuad->bind();
	programShade->use();
	m_vaoQuad->drawArrays(GL_POINTS, 0, 1);
	programShade->release();
	m_vaoQuad->unbind();

	m_shadowDepthTexture->unbindActive(11);
	m_shadowColorTexture->unbindActive(10);
	m_environmentTextures[environmentTextureIndex]->unbindActive(9);
	m_materialTextures[materialTextureIndex]->unbindActive(8);
	m_ambientTexture->unbindActive(7);
	m_depthTexture->unbindActive(6);
	m_surfaceDiffuseTexture->unbindActive(5);
	m_surfaceNormalTexture->unbindActive(4);
	m_surfacePositionTexture->unbindActive(3);
	m_sphereDiffuseTexture->unbindActive(2);
	m_sphereNormalTexture->unbindActive(1);
	m_spherePositionTexture->unbindActive(0);

	m_shadeFramebuffer->unbind();


	//////////////////////////////////////////////////////////////////////////
	// Depth of field (optional)
	//////////////////////////////////////////////////////////////////////////
	if (depthOfField)
	{
		//////////////////////////////////////////////////////////////////////////
		// Depth of field blurring -- horizontal
		//////////////////////////////////////////////////////////////////////////
		m_dofBlurFramebuffer->bind();

		m_colorTexture->bindActive(0);
		m_colorTexture->bindActive(1);

		programDOFBlur->setUniform("maximumCoCRadius", maximumCoCRadius);
		programDOFBlur->setUniform("aparture", aparture);
		programDOFBlur->setUniform("focalDistance", focalDistance);
		programDOFBlur->setUniform("focalLength", focalLength);

		programDOFBlur->setUniform("uMaxCoCRadiusPixels", (int)round(maximumCoCRadius));
		programDOFBlur->setUniform("uNearBlurRadiusPixels", (int)round(maximumCoCRadius));
		programDOFBlur->setUniform("uInvNearBlurRadiusPixels", 1.0f / maximumCoCRadius);
		programDOFBlur->setUniform("horizontal", true);
		programDOFBlur->setUniform("nearTexture", 0);
		programDOFBlur->setUniform("blurTexture", 1);

		m_vaoQuad->bind();
		programDOFBlur->use();
		m_vaoQuad->drawArrays(GL_POINTS, 0, 1);
		programDOFBlur->release();
		m_vaoQuad->unbind();

		m_colorTexture->unbindActive(1);
		m_colorTexture->unbindActive(0);
		m_dofBlurFramebuffer->unbind();


		//////////////////////////////////////////////////////////////////////////
		// Depth of field blurring -- vertical
		//////////////////////////////////////////////////////////////////////////
		m_dofFramebuffer->bind();

		m_sphereNormalTexture->bindActive(0);
		m_surfaceNormalTexture->bindActive(1);
		programDOFBlur->setUniform("horizontal", false);
		programDOFBlur->setUniform("nearTexture", 0);
		programDOFBlur->setUniform("blurTexture", 1);

		m_vaoQuad->bind();
		programDOFBlur->use();
		m_vaoQuad->drawArrays(GL_POINTS, 0, 1);
		programDOFBlur->release();
		m_vaoQuad->unbind();

		m_surfaceNormalTexture->unbindActive(1);
		m_sphereNormalTexture->unbindActive(0);

		m_dofFramebuffer->unbind();


		//////////////////////////////////////////////////////////////////////////
		// Depth of field blending
		//////////////////////////////////////////////////////////////////////////
		m_shadeFramebuffer->bind();

		m_colorTexture->bindActive(0);
		m_sphereDiffuseTexture->bindActive(1);
		m_surfaceDiffuseTexture->bindActive(2);

		programDOFBlend->setUniform("maximumCoCRadius", maximumCoCRadius);
		programDOFBlend->setUniform("aparture", aparture);
		programDOFBlend->setUniform("focalDistance", focalDistance);
		programDOFBlend->setUniform("focalLength", focalLength);

		programDOFBlend->setUniform("colorTexture", 0);
		programDOFBlend->setUniform("nearTexture", 1);
		programDOFBlend->setUniform("blurTexture", 2);

		m_vaoQuad->bind();
		programDOFBlend->use();
		m_vaoQuad->drawArrays(GL_POINTS, 0, 1);
		programDOFBlend->release();
		m_vaoQuad->unbind();

		m_surfaceDiffuseTexture->unbindActive(1);
		m_sphereDiffuseTexture->unbindActive(0);
		m_shadeFramebuffer->unbind();
	}

	if (viewportSize == viewer()->viewportSize())
	{
		// Blit final image into visible framebuffer
		m_shadeFramebuffer->blit(GL_COLOR_ATTACHMENT0, { 0,0,viewer()->viewportSize().x, viewer()->viewportSize().y }, Framebuffer::defaultFBO().get(), GL_BACK, { 0,0,viewer()->viewportSize().x, viewer()->viewportSize().y }, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
	}
	else
	{
		m_colorTexture->bindActive(0);
		m_depthTexture->bindActive(1);

		glViewport(0, 0, viewer()->viewportSize().x, viewer()->viewportSize().y);
		glDepthMask(GL_TRUE);

		programDisplay->setUniform("colorTexture", 0);
		programDisplay->setUniform("depthTexture", 1);

		m_vaoQuad->bind();
		programDisplay->use();
		m_vaoQuad->drawArrays(GL_POINTS, 0, 1);
		programDisplay->release();
		m_vaoQuad->unbind();

		m_depthTexture->unbindActive(1);
		m_colorTexture->unbindActive(0);

	}

	// Render current input position indicator

	glPointSize(17.0f);

	programCurrentInputPosition->use();
	programCurrentInputPosition->setUniform("modelViewProjectionMatrix", modelViewProjectionMatrix);
	int colour = 0;

	for (const auto &inputPosition : m_inputPositions)
	{
		if (inputPosition.enabled)
		{
			programCurrentInputPosition->setUniform("colour", colour);
			inputPosition.currentInputPositionVao->bind();
			inputPosition.currentInputPositionVao->drawArrays(GL_POINTS, 0, 1);
			inputPosition.currentInputPositionVao->unbind();
			colour++;
		}
	}

	programCurrentInputPosition->release();

	// Restore OpenGL state
	currentState->apply();
}

void SphereRenderer::InputPosition::updateCurrentInputPositionBuffer()
{
	currentInputPositionIndicatorBuffer->setSubData(0, sizeof(glm::vec3), glm::value_ptr(currentInputPosition));
}