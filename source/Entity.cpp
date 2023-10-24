#include "Entity.h"
#include "renderer/Renderer.h"

MeshObject::MeshObject(MeshHandle_t mesh_handle, MaterialHandle_t material_handle, const glm::mat4& transform)
	: m_mesh_handle(mesh_handle), m_material_handle(material_handle), m_transform(transform)
{
}

void MeshObject::Update(float dt)
{
}

void MeshObject::Render()
{
	Renderer::SubmitMesh(m_mesh_handle, m_material_handle, m_transform);
}

Pointlight::Pointlight(const glm::vec3& pos, const glm::vec3& color, float intensity)
	: m_position(pos), m_color(color), m_intensity(intensity)
{
}

void Pointlight::Update(float dt)
{
}

void Pointlight::Render()
{
	Renderer::SubmitPointlight(m_position, m_color, m_intensity);
}
