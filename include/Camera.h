#pragma once

class Camera
{
public:
	Camera() = default;
	// FOV in degrees
	Camera(const glm::vec3& pos, const glm::vec3& view_dir, float vfov);

	void Update(float dt);

	glm::mat4 GetView() const;
	float GetVerticalFOV() const;

private:
	glm::mat4 m_view = glm::identity<glm::mat4>();

	glm::vec3 m_translation = glm::vec3(0.0f);
	glm::vec3 m_rotation = glm::vec3(0.0f);
	float m_yaw = 0.0f;
	float m_pitch = 0.0f;

	float m_vfov = 60.0f;
	float m_speed = 8.0f;

};
