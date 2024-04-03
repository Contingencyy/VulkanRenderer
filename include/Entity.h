#pragma once
#include "renderer/RenderTypes.h"
#include "Assets.h"

// Entity interface
class Entity
{
public:
	Entity(const std::string& label);

	virtual void Update(float dt) = 0;
	virtual void Render() = 0;
	virtual void RenderUI() = 0;

protected:
	std::string m_label = "";

};

class MeshObject : public Entity
{
public:
	MeshObject(MeshHandle_t mesh_handle, const Assets::Material& material, const glm::mat4& transform, const std::string& label);

	virtual void Update(float dt) override;
	virtual void Render() override;
	virtual void RenderUI() override;

private:
	MeshHandle_t m_mesh_handle = {};
	Assets::Material m_material;

	glm::mat4 m_transform = glm::identity<glm::mat4>();
	glm::vec3 m_translation = glm::vec3(0.0f);
	glm::vec3 m_rotation = glm::vec3(0.0f);
	glm::vec3 m_scale = glm::vec3(1.0f);

};

class Pointlight : public Entity
{
public:
	Pointlight(const glm::vec3& pos, const glm::vec3& color, float intensity, const std::string& label);

	virtual void Update(float dt) override;
	virtual void Render() override;
	virtual void RenderUI() override;

private:
	glm::vec3 m_position = glm::vec3(0.0f);
	glm::vec3 m_color = glm::vec3(1.0f);
	float m_intensity = 1.0f;

};

class AreaLight : public Entity
{
public:
	AreaLight(TextureHandle_t texture_handle, const glm::mat4& transform, const glm::vec3& color, float intensity, bool two_sided, const std::string& label);

	virtual void Update(float dt) override;
	virtual void Render() override;
	virtual void RenderUI() override;

private:
	TextureHandle_t m_texture_handle = {};

	glm::mat4 m_transform = glm::identity<glm::mat4>();
	glm::vec3 m_translation = glm::vec3(0.0f);
	glm::vec3 m_rotation = glm::vec3(0.0f);
	glm::vec3 m_scale = glm::vec3(1.0f);

	glm::vec3 m_color = glm::vec3(0.0);
	float m_intensity = 1.0f;
	bool m_two_sided = false;

};
