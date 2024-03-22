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
	ImGui::Begin("Scene");

	for (auto& entity : m_entities)
	{
		entity->RenderUI();
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
