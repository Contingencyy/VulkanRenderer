#pragma once
#include "renderer/RenderTypes.h"

// Entity interface
class Entity
{
public:
	virtual void Update(float dt) = 0;
	virtual void Render() = 0;
};

class MeshObject : public Entity
{
public:
	MeshObject(MeshHandle_t mesh_handle, MaterialHandle_t material_handle, const glm::mat4& transform);

	virtual void Update(float dt) override;
	virtual void Render() override;

private:
	MeshHandle_t m_mesh_handle = {};
	MaterialHandle_t m_material_handle = {};

	glm::mat4 m_transform = glm::identity<glm::mat4>();

};

class Pointlight : public Entity
{
public:
	Pointlight(const glm::vec3& pos, const glm::vec3& color, float intensity);

	virtual void Update(float dt) override;
	virtual void Render() override;

private:
	glm::vec3 m_position = glm::vec3(0.0f);
	glm::vec3 m_color = glm::vec3(1.0f);
	float m_intensity = 1.0f;

};
