#pragma once
#include <iostream>

#include <glbinding/gl/gl.h>

using namespace gl;

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <array>
#include <string_view>
#include <unordered_map>

#define GL_NONE 0

// shader
class CStdShader
{
public:
	class Exception : public std::runtime_error
	{
	public:
		using runtime_error::runtime_error;
	};

	enum class Type : uint8_t
	{
		Vertex,
		TesselationControl,
		TesselationEvaluation,
		Geometry,
		Fragment,
		Compute
	};

public:
	CStdShader() = default;
	explicit CStdShader(Type type, const std::string& source) : type{ type }, source{ source } {}
	CStdShader(const CStdShader&) = delete;

	virtual ~CStdShader() { Clear(); }

	void SetMacro(std::string_view key, const std::string& value);
	void UnsetMacro(std::string_view key);
	void SetSource(const std::string& source);
	void AddInclude(const std::string& source);
	void SetType(Type type);

	virtual void Compile() = 0;
	virtual void Clear();

	std::string GetSource() const { return source; }
	virtual int64_t GetHandle() const = 0;
	std::unordered_map<std::string, std::string> GetMacros() const { return macros; }
	std::string GetErrorMessage() const { return errorMessage; }
	virtual Type GetType() const { return type; }

protected:
	Type type;
	std::string source;
	std::vector<std::string> includes;
	std::unordered_map<std::string, std::string> macros;
	std::string errorMessage;
};

class CStdGLShader : public CStdShader
{
public:
	using CStdShader::CStdShader;

	void Compile() override;
	void Clear() override;

	virtual int64_t GetHandle() const override { return shader; }

protected:
	virtual void PrepareSource();

protected:
	GLuint shader = 0;
};

class CStdShaderProgram
{
public:
	class Exception : public std::runtime_error
	{
	public:
		using runtime_error::runtime_error;
	};

public:
	CStdShaderProgram() = default;
	CStdShaderProgram(const CStdShaderProgram&) = delete;
	virtual ~CStdShaderProgram() { Clear(); }

	virtual explicit operator bool() const = 0;

	bool AddShader(CStdShader* shader);

	virtual void Link() = 0;
	void Select();
	static void Deselect();
	virtual void Clear();

	virtual void EnsureProgram() = 0;
	virtual int64_t GetProgram() const = 0;

	std::vector<CStdShader*> GetPendingShaders() const { return shaders; }
	static CStdShaderProgram* GetCurrentShaderProgram();

protected:
	virtual bool AddShaderInt(CStdShader* shader) = 0;
	virtual void OnSelect() = 0;
	virtual void OnDeselect() = 0;

protected:
	std::vector<CStdShader*> shaders;
	std::string errorMessage;
	static CStdShaderProgram* currentShaderProgram;
};

class CStdGLShaderProgram : public CStdShaderProgram
{
public:
	using CStdShaderProgram::CStdShaderProgram;

	explicit operator bool() const override { return /*glIsProgram(*/shaderProgram/*)*/; }

	void Link() override;
	void Clear() override;

	void EnsureProgram() override;

	template<typename Func, typename... Args> bool SetAttribute(std::string_view key, Func function, Args... args)
	{
		return SetAttribute(key, &CStdGLShaderProgram::attributeLocations, glGetAttribLocation, function, args...);
	}

	template<typename Func, typename... Args> bool SetUniform(std::string_view key, Func function, Args... args)
	{
		return SetAttribute(key, &CStdGLShaderProgram::uniformLocations, glGetUniformLocation, function, args...);
	}

	bool SetUniform(std::string_view key, float value) { return SetUniform(key, glUniform1f, value); }
	bool SetUniform(std::string_view key, const glm::vec2& value);
	bool SetUniform(std::string_view key, const glm::vec3& value);
	bool SetUniform(std::string_view key, const glm::vec4& value);
	bool SetUniform(std::string_view key, const glm::mat4& value);

	void EnterGroup(const std::string& name) { group.assign(name).append("."); }
	void LeaveGroup() { group.clear(); }

	void SetObjectLabel(std::string_view label);

	virtual int64_t GetProgram() const override { return shaderProgram; }

protected:
	bool AddShaderInt(CStdShader* shader) override;
	void OnSelect() override;
	void OnDeselect() override;

	using Locations = std::unordered_map<std::string, GLint>;
	template<typename MapFunc, typename SetFunc, typename... Args> bool SetAttribute(std::string_view key, Locations CStdGLShaderProgram::* locationPointer, MapFunc mapFunction, SetFunc setFunction, Args... args)
	{
		assert(shaderProgram);

		std::string realKey{ group };
		realKey.append(key.data());

		GLint location;
		Locations& locations{ this->*locationPointer };
		if (auto it = locations.find(realKey); it != locations.end())
		{
			location = it->second;
			assert(location != -1);
		}
		else
		{
			location = mapFunction(shaderProgram, realKey.c_str());
			if (location == -1)
			{
				return false;
			}

			locations.emplace(realKey, location);
		}
		setFunction(location, args...);

		return true;
	}

protected:
	GLuint shaderProgram{ 0 };

	Locations attributeLocations;
	Locations uniformLocations;

	std::string group;
};

template<typename Class>
class CStdVAOObject
{
public:
	CStdVAOObject() = default;
	virtual ~CStdVAOObject()
	{
		glDeleteVertexArrays(1, &VAO);
		glDeleteBuffers(VBO.size(), VBO.data());
	}

public:
	void Bind() const
	{
		glBindVertexArray(VAO);
	}
	void Draw() const
	{
		glDrawElements(Class::PrimitiveType, elementCount, GL_UNSIGNED_INT, nullptr);
	}

protected:
	void Init()
	{
		assert(!VAO);
		glGenVertexArrays(1, &VAO);
		glGenBuffers(VBO.size(), VBO.data());

		glBindVertexArray(VAO);
		glBindBuffer(GL_ARRAY_BUFFER, VBO[0]);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, VBO[VBO.size() - 1]);

		std::vector<GLfloat> vertices;
		std::vector<GLuint> elements;
		std::vector<GLfloat> normals;
		std::vector<GLfloat> textureCoordinates;
		GenerateGeometry(vertices, elements, normals, textureCoordinates);
		elementCount = elements.size();

		constexpr std::size_t dimensions{Class::Dimensions};

		glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(GLfloat), vertices.data(), GL_STATIC_DRAW);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, elementCount * sizeof(GLuint), elements.data(), GL_STATIC_DRAW);

		glVertexAttribPointer(0, dimensions, GL_FLOAT, GL_FALSE, dimensions * sizeof(GLfloat), nullptr);
		glEnableVertexAttribArray(0);

		glBindBuffer(GL_ARRAY_BUFFER, VBO[1]);
		glBufferData(GL_ARRAY_BUFFER, normals.size() * sizeof(GLfloat), normals.data(), GL_STATIC_DRAW);
		glVertexAttribPointer(1, dimensions, GL_FLOAT, GL_FALSE, dimensions * sizeof(GLfloat), nullptr);
		glEnableVertexAttribArray(1);

		glBindBuffer(GL_ARRAY_BUFFER, VBO[2]);
		glBufferData(GL_ARRAY_BUFFER, textureCoordinates.size() * sizeof(GLfloat), textureCoordinates.data(), GL_STATIC_DRAW);
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), nullptr);
		glEnableVertexAttribArray(2);

		glBindVertexArray(GL_NONE);
		glBindBuffer(GL_ARRAY_BUFFER, GL_NONE);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, GL_NONE);
	}

	virtual void GenerateGeometry(std::vector<GLfloat> &vertices, std::vector<GLuint> &indices, std::vector<GLfloat> &normals, std::vector<GLfloat> &textureCoordinates) = 0;
private:
	std::size_t elementCount{0};
	GLuint VAO{GL_NONE};
	std::array<GLuint, 4> VBO{GL_NONE};
};



class CStdRectangle : public CStdVAOObject<CStdRectangle>
{
public:
    static constexpr inline auto Dimensions = 2;
    static constexpr inline auto PrimitiveType = GL_TRIANGLES;

public:
	CStdRectangle() : CStdVAOObject{} { Init(); }

protected:
	virtual void GenerateGeometry(std::vector<GLfloat> &vertices, std::vector<GLuint> &elements, std::vector<GLfloat> &normals, std::vector<GLfloat> &textureCoordinates) override;
};

template<GLenum Target, std::size_t Dimensions>
class CStdTexture;

template<GLenum Target, std::size_t Dimensions>
struct CStdTextureHelper
{
	static void SetData(const CStdTexture<Target, Dimensions> &texture, void *const data);
};

template<GLenum T, std::size_t Dimensions>
class CStdTexture
{
private:
	friend struct CStdTextureHelper<T, Dimensions>;

public:
	static constexpr GLenum Target{T};

public:
	CStdTexture() : dimensions{}, texture{GL_NONE}, internalFormat{GL_NONE}, format{GL_NONE}, type{GL_NONE} {}
	// https://stackoverflow.com/questions/3279543/what-is-the-copy-and-swap-idiom/3279550#3279550
	CStdTexture(const std::array<std::int32_t, Dimensions> &dimensions, GLenum internalFormat, GLenum format, GLenum type, void *const data = nullptr)
		: dimensions{dimensions}, internalFormat{internalFormat}, format{format}, type{type}
	{
		glGenTextures(1, &texture);
		glBindTexture(Target, texture);
		glTexParameteri(Target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(Target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

		SetData(data);
	}

	CStdTexture(const CStdTexture &) = delete;
	CStdTexture(CStdTexture&& other) : CStdTexture{}
	{
		swap(*this, other);
	}

	~CStdTexture()
	{
		if (texture)
		{
			glDeleteTextures(1, &texture);
		}
	}

	CStdTexture& operator=(const CStdTexture& other) = delete;
	CStdTexture& operator=(CStdTexture &&other)
	{
		swap(*this, other);
		return *this;
	}

	template<GLenum T, std::size_t D> friend void swap(CStdTexture<T, D> &first, CStdTexture<T, D> &second);

public:
	void Bind(GLuint offset) const
	{
		glActiveTexture(GL_TEXTURE0 + offset);

		glBindTexture(Target, texture);
	}

	void SetData(void *const data) const
	{
		CStdTextureHelper<Target, Dimensions>::SetData(*this, data);
	}

	GLuint GetTexture() const { return texture; }

protected:
	GLuint texture{GL_NONE};
	std::array<std::int32_t, Dimensions> dimensions;
	GLenum internalFormat;
	GLenum format;
	GLenum type;
};

template<GLenum Target, std::size_t Dimensions> inline void swap(CStdTexture<Target, Dimensions> &first, CStdTexture<Target, Dimensions> &second)
{
	using std::swap;

	swap(first.texture, second.texture);
	swap(first.dimensions, second.dimensions);
	swap(first.internalFormat, second.internalFormat);
	swap(first.format, second.format);
	swap(first.type, second.type);
}

template<GLenum Target>
struct CStdTextureHelper<Target, 2>
{
	static void SetData(const CStdTexture<Target, 2> &texture, void *const data)
	{
		glTexImage2D(Target, 0, texture.internalFormat, texture.dimensions[0], texture.dimensions[1], 0, texture.format, texture.type, data);
	}
};

template<GLenum Target>
struct CStdTextureHelper<Target, 3>
{
	static void SetData(const CStdTexture<Target, 3> &texture, void *const data)
	{
		glTexImage3D(Target, 0, texture.internalFormat, texture.dimensions[0], texture.dimensions[1], texture.dimensions[2], 0, texture.format, texture.type, data);
	}
};

class CStdTexture3D : public CStdTexture<GL_TEXTURE_3D, 3>
{
public:
	CStdTexture3D() : CStdTexture{} {}
	CStdTexture3D(const std::int32_t width, const std::int32_t height,const std::int32_t depth, const GLenum internalFormat, const GLenum format, const GLenum type, void *const data = nullptr)
		: CStdTexture{{width, height, depth}, internalFormat, format, type, data} {}

	CStdTexture3D(const std::int32_t width, const std::int32_t height, const std::int32_t depth, const std::uint8_t channels, const bool snorm = false, const bool use32BitComponentWidth = false, void *const data = nullptr)
		: CStdTexture3D{width, height, depth, snorm ? InternalSnormFormats.at(channels - 1) : InternalFormats.at(channels - 1)[use32BitComponentWidth], Formats[channels - 1], GL_FLOAT, data} {}

	CStdTexture3D(const CStdTexture3D &) = delete;
	CStdTexture3D(CStdTexture3D&& other) : CStdTexture3D{}
	{
		swap(*this, other);
	}

	CStdTexture3D &operator=(const CStdTexture3D &other) = delete;
	CStdTexture3D &operator=(CStdTexture3D &&other)
	{
		swap(*this, other);
		return *this;
	}

public:
	void BindImage(GLuint unit, GLenum access) const;

private:
		static constexpr std::array<GLenum, 4> Formats{GL_RED, GL_RG, GL_RGB, GL_RGBA};
		static constexpr std::array<std::array<GLenum, 2>, 4> InternalFormats
		{{
			{GL_R32F, GL_R16F},
			{GL_RG32F, GL_RG16F},
			{GL_RGB32F, GL_RGB16F},
			{GL_RGBA32F, GL_RGBA16F}
		}};
		static constexpr std::array<GLenum, 4> InternalSnormFormats{GL_R16_SNORM, GL_RG16_SNORM, GL_RGB16_SNORM, GL_RGBA16_SNORM};
		static constexpr auto Type = GL_FLOAT;
};

template<typename T>
class CStdSwappable
{
public:
	template<typename... Args> CStdSwappable(Args... args) : buffer1{args...}, buffer2{args...}, front{&buffer1}, back{&buffer2} {}

	CStdSwappable(CStdSwappable<T> &&other) : CStdSwappable{}
	{
		swap(*this, other);
	}

	CStdSwappable<T> &operator=(CStdSwappable<T> &&other)
	{
		swap(*this, other);
		return *this;
	}

	template<typename T>
	friend void swap(CStdSwappable<T> &first, CStdSwappable<T> &second);

public:
	void SwapBuffers()
	{
		using std::swap;
		swap(front, back);
	}

	const T &GetFront() const { return *front; }
	T &GetFront() { return *front; }
	const T &GetBack() const { return *back; }
	T &GetBack() { return *back; }

private:
	T buffer1;
	T buffer2;
	T *front;
	T *back;
};

template<typename T>
inline void swap(CStdSwappable<T> &first, CStdSwappable<T> &second)
{
	using std::swap;
	swap(first.buffer1, second.buffer1);
	swap(first.buffer2, second.buffer2);
	first.front = second.front == &second.buffer1 ? &first.buffer1 : &first.buffer2;
	first.back = second.back == &second.buffer2 ? &first.buffer2 : &first.buffer1;
}


class CStdFramebuffer
{
public:
	CStdFramebuffer() : colorAttachment{}, FBO{GL_NONE} {}
	CStdFramebuffer(std::int32_t width, std::int32_t height);
	~CStdFramebuffer();

	CStdFramebuffer(CStdFramebuffer &&other) : CStdFramebuffer{}
	{
		swap(*this, other);
	}

	CStdFramebuffer &operator=(CStdFramebuffer &&other)
	{
		swap(*this, other);
		return *this;
	}

	friend void swap(CStdFramebuffer &first, CStdFramebuffer &second)
	{
		using std::swap;
		swap(first.colorAttachment, second.colorAttachment);
		swap(first.FBO, second.FBO);
	}

public:
	void Bind() const;
	void BindTexture(GLuint offset) const;
	void Unbind() const;
	const CStdTexture<GL_TEXTURE_2D, 2> &GetTexture() const { return colorAttachment; }

	//void Resize(std::int32_t newWidth, std::int32_t newHeight, CStdGLShaderProgram &copyShader, CStdRectangle &rectangle);

private:
	static constexpr inline auto InternalFormat = GL_RG16F;
	static constexpr inline auto Format = GL_RG;
	static constexpr inline auto Type = GL_FLOAT;

	CStdTexture<GL_TEXTURE_2D, 2> colorAttachment;
	GLuint FBO;
};

class CStdSwappableFramebuffer : public CStdSwappable<CStdFramebuffer>
{
public:
	using CStdSwappable<CStdFramebuffer>::CStdSwappable;

public:
	void Bind() const;
	void Unbind() const;
};

class CStdSwappableTexture3D : public CStdSwappable<CStdTexture3D>
{
public:
	using CStdSwappable<CStdTexture3D>::CStdSwappable;

public:
	void Bind(GLuint offset) const;
};