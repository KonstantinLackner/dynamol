#pragma once
#include "Renderer.h"
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
#include <globjects/TransformFeedback.h>
#include <globjects/NamedString.h>
#include <globjects/base/StaticStringSource.h>

namespace dynamol
{
	class Viewer;

	class SphereRenderer : public Renderer
	{
	private:
		struct Molecule
		{
			std::pair<std::unique_ptr<globjects::Buffer>, std::unique_ptr<globjects::Buffer>> vertices;
			std::size_t numberOfAtoms;

			Molecule(const std::vector<glm::vec4> &atoms);

			void addAtoms(const std::vector<glm::vec3> &atoms);
		};

	public:
		SphereRenderer(Viewer *viewer);

		virtual void display();

	private:
		void updateCurrentInputPositionBuffer();

	private:
		std::vector<Molecule> m_molecules;
		std::unique_ptr<globjects::VertexArray> m_vao = std::make_unique<globjects::VertexArray>();
		std::unique_ptr<globjects::Buffer> m_elementColorsRadii = std::make_unique<globjects::Buffer>();
		std::unique_ptr<globjects::Buffer> m_residueColors = std::make_unique<globjects::Buffer>();
		std::unique_ptr<globjects::Buffer> m_chainColors = std::make_unique<globjects::Buffer>();
		
		std::unique_ptr<globjects::VertexArray> m_vaoQuad = std::make_unique<globjects::VertexArray>();
		std::unique_ptr<globjects::Buffer> m_verticesQuad = std::make_unique<globjects::Buffer>();
		
		std::unique_ptr<globjects::StaticStringSource> m_shaderSourceDefines = nullptr;
		std::unique_ptr<globjects::NamedString> m_shaderDefines = nullptr;

		std::unique_ptr<globjects::Buffer> m_intersectionBuffer = std::make_unique<globjects::Buffer>();
		std::unique_ptr<globjects::Buffer> m_statisticsBuffer = std::make_unique<globjects::Buffer>();
		std::unique_ptr<globjects::Texture> m_offsetTexture = nullptr;
		std::unique_ptr<globjects::Texture> m_depthTexture = nullptr;
		std::unique_ptr<globjects::Texture> m_spherePositionTexture = nullptr;
		std::unique_ptr<globjects::Texture> m_sphereNormalTexture = nullptr;
		std::unique_ptr<globjects::Texture> m_surfacePositionTexture = nullptr;
		std::unique_ptr<globjects::Texture> m_surfaceNormalTexture = nullptr;
		std::unique_ptr<globjects::Texture> m_sphereDiffuseTexture = nullptr;
		std::unique_ptr<globjects::Texture> m_surfaceDiffuseTexture = nullptr;
		std::unique_ptr<globjects::Texture> m_ambientTexture = nullptr;
		std::unique_ptr<globjects::Texture> m_blurTexture = nullptr;
		std::unique_ptr<globjects::Texture> m_colorTexture = nullptr;
		std::unique_ptr<globjects::Texture> m_shadowColorTexture = nullptr;
		std::unique_ptr<globjects::Texture> m_shadowDepthTexture = nullptr;

		std::unique_ptr<globjects::Framebuffer> m_sphereFramebuffer = nullptr;
		std::unique_ptr<globjects::Framebuffer> m_surfaceFramebuffer = nullptr;
		std::unique_ptr<globjects::Framebuffer> m_ambientFramebuffer = nullptr;
		std::unique_ptr<globjects::Framebuffer> m_shadeFramebuffer = nullptr;
		std::unique_ptr<globjects::Framebuffer> m_aoFramebuffer = nullptr;
		std::unique_ptr<globjects::Framebuffer> m_aoBlurFramebuffer = nullptr;
		std::unique_ptr<globjects::Framebuffer> m_dofFramebuffer = nullptr;
		std::unique_ptr<globjects::Framebuffer> m_dofBlurFramebuffer = nullptr;
		std::unique_ptr<globjects::Framebuffer> m_shadowFramebuffer = nullptr;

		std::vector< std::unique_ptr<globjects::Texture> > m_environmentTextures;
		std::vector< std::unique_ptr<globjects::Texture> > m_materialTextures;
		std::vector< std::unique_ptr<globjects::Texture> > m_bumpTextures;

		std::unique_ptr<globjects::TransformFeedback> m_transformFeedback = nullptr;

		glm::ivec2 m_shadowMapSize = glm::ivec2(512, 512);
		glm::ivec2 m_framebufferSize;
		float m_resolutionScale{1.0f};

		glm::vec3 m_currentInputPosition;
		std::unique_ptr<globjects::VertexArray> m_currentInputPositionVao = std::make_unique<globjects::VertexArray>();
		std::unique_ptr<globjects::Buffer> m_currentInputPositionIndicatorBuffer;
		std::int32_t m_numberOfAtomsToAdd{1};
		glm::vec3 m_currentInputForce{1.0f, 1.0f, 1.0f};

		enum InputMode : std::int32_t
		{
			IM_Atom,
			IM_Force,
		};

		std::int32_t m_inputMode{IM_Atom};
		std::int32_t m_holdForFrames{1};
		std::int32_t m_remainingFrames{1};
		bool running{false};
	};

}