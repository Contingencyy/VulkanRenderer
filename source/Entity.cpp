#include "Precomp.h"
#include "Entity.h"
#include "renderer/Renderer.h"
#include "assets/AssetManager.h"

#include "imgui/imgui.h"

Entity::Entity(const std::string& label)
	: m_label(label)
{
}

ModelObject::ModelObject(const std::string& label)
	: ModelObject(AssetHandle(), glm::identity<glm::mat4>(), label)
{
}

ModelObject::ModelObject(AssetHandle model_asset_handle, const glm::mat4& transform, const std::string& label)
	: Entity(label), m_model_asset_handle(model_asset_handle), m_transform(transform)
{
	glm::vec3 skew(0.0f);
	glm::vec4 perspective(0.0f);
	glm::quat orientation = {};
	glm::decompose(m_transform, m_scale, orientation, m_translation, skew, perspective);

	m_rotation = glm::eulerAngles(orientation);
	m_rotation = glm::degrees(m_rotation);
}

void ModelObject::Update(float dt)
{
}

static void SubmitModelNode(const ModelAsset* model_asset, const ModelAsset::Node& node, const glm::mat4& transform)
{
	for (const uint32_t mesh_index : node.mesh_indices)
	{
		const MeshAsset& mesh = model_asset->mesh_assets[mesh_index];
		const MaterialAsset& material = model_asset->material_assets[mesh.material_index];

		Renderer::SubmitMesh(mesh.mesh_render_handle, material, transform);
	}
}

void ModelObject::Render()
{
	ModelAsset* model_asset = AssetManager::GetAsset<ModelAsset>(m_model_asset_handle);
	if (!model_asset)
	{
		Renderer::SubmitMesh(RenderResourceHandle(), MaterialAsset(), m_transform);
		return;
	}

	for (const uint32_t root_node_index : model_asset->root_nodes)
	{
		ModelAsset::Node& current_node = model_asset->nodes[root_node_index];
		glm::mat4 current_transform = m_transform * current_node.transform;

		SubmitModelNode(model_asset, current_node, current_transform);

		for (const uint32_t child_node_index : model_asset->nodes[root_node_index].children)
		{
			current_node = model_asset->nodes[child_node_index];
			current_transform = current_transform * current_node.transform;

			SubmitModelNode(model_asset, current_node, current_transform);
		}
	}
}

void ModelObject::RenderUI()
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

			float texture_preview_width = std::min(ImGui::GetWindowSize().x, 256.0f);
			float texture_preview_height = std::min(ImGui::GetWindowSize().y, 256.0f);

			/*if (VK_RESOURCE_HANDLE_VALID(m_material.tex_albedo_render_handle))
				Renderer::ImGuiImage(m_material.tex_albedo_render_handle, texture_preview_width, texture_preview_height);
			ImGui::ColorEdit3("Albedo factor", &m_material.albedo_factor.x, ImGuiColorEditFlags_DisplayRGB);

			if (VK_RESOURCE_HANDLE_VALID(m_material.tex_normal_render_handle))
				Renderer::ImGuiImage(m_material.tex_normal_render_handle, texture_preview_width, texture_preview_height);

			if (VK_RESOURCE_HANDLE_VALID(m_material.tex_metal_rough_render_handle))
				Renderer::ImGuiImage(m_material.tex_metal_rough_render_handle, texture_preview_width, texture_preview_height);
			ImGui::SliderFloat("Metallic factor", &m_material.metallic_factor, 0.0f, 1.0f);
			ImGui::SliderFloat("Roughness factor", &m_material.roughness_factor, 0.0f, 1.0f);

			ImGui::Checkbox("Clearcoat", &m_material.has_clearcoat);
			if (VK_RESOURCE_HANDLE_VALID(m_material.tex_cc_alpha_render_handle))
				Renderer::ImGuiImage(m_material.tex_cc_alpha_render_handle, texture_preview_width, texture_preview_height);
			ImGui::SliderFloat("Clearcoat alpha factor", &m_material.clearcoat_alpha_factor, 0.0f, 1.0f);
			if (VK_RESOURCE_HANDLE_VALID(m_material.tex_cc_normal_render_handle))
				Renderer::ImGuiImage(m_material.tex_cc_normal_render_handle, texture_preview_width, texture_preview_height);
			if (VK_RESOURCE_HANDLE_VALID(m_material.tex_cc_rough_render_handle))
				Renderer::ImGuiImage(m_material.tex_cc_rough_render_handle, texture_preview_width, texture_preview_height);
			ImGui::SliderFloat("Clearcoat roughness factor", &m_material.clearcoat_roughness_factor, 0.0f, 1.0f);*/

			ImGui::Unindent(10.0f);
		}

		ImGui::Unindent(10.0f);
		ImGui::PopID();
	}
}

//Pointlight::Pointlight(const glm::vec3& pos, const glm::vec3& color, float intensity, const std::string& label)
//	: Entity(label), m_position(pos), m_color(color), m_intensity(intensity)
//{
//}
//
//void Pointlight::Update(float dt)
//{
//}
//
//void Pointlight::Render()
//{
//	Renderer::SubmitPointlight(m_position, m_color, m_intensity);
//}
//
//void Pointlight::RenderUI()
//{
//	if (ImGui::CollapsingHeader(m_label.c_str()))
//	{
//		ImGui::PushID(m_label.c_str());
//		ImGui::Indent(10.0f);
//
//		ImGui::DragFloat3("Position", &m_position[0], 0.01f, -FLT_MAX, FLT_MAX, "%.2f");
//		ImGui::ColorEdit3("Color", &m_color[0], ImGuiColorEditFlags_DisplayRGB);
//		ImGui::DragFloat("Intensity", &m_intensity, 0.01f, 0.0f, 10000.0f, "%.2f");
//
//		ImGui::Unindent(10.0f);
//		ImGui::PopID();
//	}
//}

AreaLight::AreaLight(const std::string& label)
	: AreaLight(AssetHandle(), glm::identity<glm::mat4>(), glm::vec3(1.0f), 5.0f, true, label)
{
}

AreaLight::AreaLight(AssetHandle texture_asset_handle, const glm::mat4& transform, const glm::vec3& color, float intensity, bool two_sided, const std::string& label)
	: Entity(label), m_texture_asset_handle(texture_asset_handle), m_transform(transform), m_color(color), m_intensity(intensity), m_two_sided(two_sided)
{
	glm::vec3 skew(0.0f);
	glm::vec4 perspective(0.0f);
	glm::quat orientation = {};
	glm::decompose(m_transform, m_scale, orientation, m_translation, skew, perspective);

	m_rotation = glm::eulerAngles(orientation);
	m_rotation = glm::degrees(m_rotation);
}

void AreaLight::Update(float dt)
{
}

void AreaLight::Render()
{
	RenderResourceHandle texture_render_handle = RenderResourceHandle();

	TextureAsset* texture_asset = AssetManager::GetAsset<TextureAsset>(m_texture_asset_handle);
	if (texture_asset)
		texture_render_handle = texture_asset->texture_render_handle;

	Renderer::SubmitAreaLight(texture_render_handle, m_transform, m_color, m_intensity, m_two_sided);
}

void AreaLight::RenderUI()
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

			float texture_preview_width = std::min(ImGui::GetWindowSize().x, 256.0f);
			float texture_preview_height = std::min(ImGui::GetWindowSize().y, 256.0f);

			RenderResourceHandle texture_render_handle = RenderResourceHandle();

			TextureAsset* texture_asset = AssetManager::GetAsset<TextureAsset>(m_texture_asset_handle);
			if (texture_asset)
				texture_render_handle = texture_asset->texture_render_handle;

			Renderer::ImGuiImage(texture_render_handle, texture_preview_width, texture_preview_height);

			ImGui::ColorEdit3("Color", &m_color[0], ImGuiColorEditFlags_DisplayRGB);
			ImGui::DragFloat("Intensity", &m_intensity, 0.01f, 0.0f, 10000.0f, "%.2f");
			ImGui::Checkbox("Two-sided", &m_two_sided);

			ImGui::Unindent(10.0f);
		}

		ImGui::Unindent(10.0f);
		ImGui::PopID();
	}
}
