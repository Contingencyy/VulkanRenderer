#include "Precomp.h"
#include "Scene.h"
#include "Entity.h"

#include "imgui/imgui.h"

Scene::Scene()
{
	m_active_camera = Camera(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), 60.0f);
}

void Scene::Update(float dt)
{
	m_active_camera.Update(dt);

	for (auto& entity : m_entities)
	{
		entity->Update(dt);
	}
}

void Scene::Render()
{
	for (auto& entity : m_entities)
	{
		entity->Render();
	}
}

void Scene::RenderUI()
{
	if (ImGui::Begin("Scene", nullptr, ImGuiWindowFlags_MenuBar))
	{
		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu("Entities"))
			{
				if (ImGui::BeginMenu("New Entity"))
				{
					if (ImGui::MenuItem("Mesh"))
					{
						AddEntity<MeshObject>("Mesh");
					}
					if (ImGui::MenuItem("AreaLight"))
					{
						AddEntity<AreaLight>("AreaLight");
					}
					ImGui::EndMenu();
				}

				ImGui::EndMenu();
			}

			ImGui::EndMenuBar();
		}

		// Scene Hierarchy UI
		if (ImGui::CollapsingHeader("Scene Hierarchy"))
		{
			for (auto& entity : m_entities)
			{
				entity->RenderUI();
			}
		}
	}
	ImGui::End();
}

Camera& Scene::GetActiveCamera()
{
	return m_active_camera;
}

const Camera& Scene::GetActiveCamera() const
{
	return m_active_camera;
}
