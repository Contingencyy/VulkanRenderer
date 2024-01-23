#pragma once
#include "Camera.h"
#include "Entity.h"

class Scene
{
public:
	Scene();

	void Update(float dt);
	void Render();
	void RenderUI();

	template<typename T, typename... TArgs>
	void AddEntity(TArgs&&... args)
	{
		m_entities.emplace_back(std::make_unique<T>(std::forward<TArgs>(args)...));
	}

	Camera& GetActiveCamera();
	const Camera& GetActiveCamera() const;

private:
	Camera m_active_camera;
	std::vector<std::unique_ptr<Entity>> m_entities;

};
