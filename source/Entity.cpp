#include "Entity.h"
#include "renderer/Renderer.h"

#include "imgui/imgui.h"

Entity::Entity(const std::string& label)
	: m_label(label)
{
}

MeshObject::MeshObject(MeshHandle_t mesh_handle, MaterialHandle_t material_handle, const glm::mat4& transform, const std::string& label)
	: Entity(label), m_mesh_handle(mesh_handle), m_material_handle(material_handle), m_transform(transform)
{
	glm::vec3 skew(0.0f);
	glm::vec4 perspective(0.0f);
	glm::quat orientation = {};
	glm::decompose(m_transform, m_scale, orientation, m_translation, skew, perspective);

	m_rotation = glm::eulerAngles(orientation);
	m_rotation = glm::degrees(m_rotation);
}

void MeshObject::Update(float dt)
{
}

void MeshObject::Render()
{
	Renderer::SubmitMesh(m_mesh_handle, m_material_handle, m_transform);
}

void MeshObject::RenderUI()
{
	if (ImGui::CollapsingHeader(m_label.c_str()))
	{
		bool transform_changed = false;

		ImGui::PushID(m_label.c_str());
		ImGui::Indent(10.0f);

		if (ImGui::DragFloat3("Translation", &m_translation[0], 0.01f, -FLT_MAX, FLT_MAX, "%.2f"))
		{
			transform_changed = true;
		}
		if (ImGui::DragFloat3("Rotation", &m_rotation[0], 0.01f, -360.0f, 360.0f, "%.2f"))
		{
			transform_changed = true;
		}
		if (ImGui::DragFloat3("Scale", &m_scale[0], 0.01f, 0.001f, 10000.0f, "%.2f"))
		{
			transform_changed = true;
		}

		if (transform_changed)
		{
			m_transform = glm::translate(glm::identity<glm::mat4>(), m_translation);
			m_transform = glm::rotate(m_transform, glm::radians(m_rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
			m_transform = glm::rotate(m_transform, glm::radians(m_rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
			m_transform = glm::rotate(m_transform, glm::radians(m_rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
			m_transform = glm::scale(m_transform, m_scale);
		}

		ImGui::Unindent(10.0f);
		ImGui::PopID();
	}
}

Pointlight::Pointlight(const glm::vec3& pos, const glm::vec3& color, float intensity, const std::string& label)
	: Entity(label), m_position(pos), m_color(color), m_intensity(intensity)
{
}

void Pointlight::Update(float dt)
{
}

void Pointlight::Render()
{
	Renderer::SubmitPointlight(m_position, m_color, m_intensity);
}

void Pointlight::RenderUI()
{
	if (ImGui::CollapsingHeader(m_label.c_str()))
	{
		ImGui::PushID(m_label.c_str());
		ImGui::Indent(10.0f);

		ImGui::DragFloat3("Position", &m_position[0], 0.01f, -FLT_MAX, FLT_MAX, "%.2f");
		ImGui::ColorEdit3("Color", &m_color[0], ImGuiColorEditFlags_DisplayRGB);
		ImGui::DragFloat("Intensity", &m_intensity, 0.01f, 0.0f, 10000.0f, "%.2f");

		ImGui::Unindent(10.0f);
		ImGui::PopID();
	}
}
