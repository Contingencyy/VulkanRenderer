#include "Precomp.h"
#include "Entity.h"
#include "renderer/Renderer.h"

#include "imgui/imgui.h"

Entity::Entity(const std::string& label)
	: m_label(label)
{
}

MeshObject::MeshObject(MeshHandle_t mesh_handle, const Assets::Material& material, const glm::mat4& transform, const std::string& label)
	: Entity(label), m_mesh_handle(mesh_handle), m_material(material), m_transform(transform)
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
	Renderer::SubmitMesh(m_mesh_handle, m_material, m_transform);
}

void MeshObject::RenderUI()
{
	if (ImGui::CollapsingHeader(m_label.c_str()))
	{
		ImGui::PushID(m_label.c_str());
		ImGui::Indent(10.0f);

		if (ImGui::CollapsingHeader("Transform"))
		{
			ImGui::Indent(10.0f);

			bool transform_changed = false;
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
		}

		if (ImGui::CollapsingHeader("Material"))
		{
			ImGui::Indent(10.0f);

			if (VK_RESOURCE_HANDLE_VALID(m_material.albedo_texture_handle))
				Renderer::ImGuiRenderTexture(m_material.albedo_texture_handle);
			ImGui::ColorEdit3("Albedo factor", &m_material.albedo_factor.x, ImGuiColorEditFlags_DisplayRGB);

			if (VK_RESOURCE_HANDLE_VALID(m_material.normal_texture_handle))
				Renderer::ImGuiRenderTexture(m_material.normal_texture_handle);

			if (VK_RESOURCE_HANDLE_VALID(m_material.metallic_roughness_texture_handle))
				Renderer::ImGuiRenderTexture(m_material.metallic_roughness_texture_handle);
			ImGui::SliderFloat("Metallic factor", &m_material.metallic_factor, 0.0f, 1.0f);
			ImGui::SliderFloat("Roughness factor", &m_material.roughness_factor, 0.0f, 1.0f);

			ImGui::Checkbox("Clearcoat", &m_material.has_clearcoat);
			if (VK_RESOURCE_HANDLE_VALID(m_material.clearcoat_alpha_texture_handle))
				Renderer::ImGuiRenderTexture(m_material.clearcoat_alpha_texture_handle);
			ImGui::SliderFloat("Clearcoat alpha factor", &m_material.clearcoat_alpha_factor, 0.0f, 1.0f);
			if (VK_RESOURCE_HANDLE_VALID(m_material.clearcoat_normal_texture_handle))
				Renderer::ImGuiRenderTexture(m_material.clearcoat_normal_texture_handle);
			if (VK_RESOURCE_HANDLE_VALID(m_material.clearcoat_roughness_texture_handle))
				Renderer::ImGuiRenderTexture(m_material.clearcoat_roughness_texture_handle);
			ImGui::SliderFloat("Clearcoat roughness factor", &m_material.clearcoat_roughness_factor, 0.0f, 1.0f);

			ImGui::Unindent(10.0f);
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

AreaLight::AreaLight(const glm::vec3 verts[4], const glm::vec3& color, float intensity, bool two_sided, const std::string& label)
	: Entity(label), m_color(color), m_intensity(intensity), m_two_sided(two_sided)
{
	memcpy(m_vertices, verts, 4 * sizeof(glm::vec3));
}

void AreaLight::Update(float dt)
{
}

void AreaLight::Render()
{
	Renderer::SubmitAreaLight(m_vertices, m_color, m_intensity, m_two_sided);
}

void AreaLight::RenderUI()
{
	if (ImGui::CollapsingHeader(m_label.c_str()))
	{
		ImGui::PushID(m_label.c_str());
		ImGui::Indent(10.0f);

		ImGui::DragFloat3("Vertex 0", &m_vertices[0].x, 0.01f, -FLT_MAX, FLT_MAX, "%.2f");
		ImGui::DragFloat3("Vertex 1", &m_vertices[1].x, 0.01f, -FLT_MAX, FLT_MAX, "%.2f");
		ImGui::DragFloat3("Vertex 2", &m_vertices[2].x, 0.01f, -FLT_MAX, FLT_MAX, "%.2f");
		ImGui::DragFloat3("Vertex 3", &m_vertices[3].x, 0.01f, -FLT_MAX, FLT_MAX, "%.2f");

		ImGui::ColorEdit3("Color", &m_color[0], ImGuiColorEditFlags_DisplayRGB);
		ImGui::DragFloat("Intensity", &m_intensity, 0.01f, 0.0f, 10000.0f, "%.2f");
		ImGui::Checkbox("Two-sided", &m_two_sided);

		ImGui::Unindent(10.0f);
		ImGui::PopID();
	}
}
