#pragma once

class Camera
{
public:
	Camera() = default;
	// FOV in degrees
	Camera(const glm::vec3& pos, const glm::vec3& view_dir, float fov, float aspect);

	void Update(float dt);

	void OnResolutionChanged(uint32_t new_width, uint32_t new_height);
	
	glm::mat4 GetView() const;
	glm::mat4 GetProjection() const;

private:
	glm::mat4 m_view = glm::identity<glm::mat4>();
	glm::mat4 m_projection = glm::identity<glm::mat4>();

	glm::vec3 m_translation = glm::vec3(0.0f);
	glm::vec3 m_rotation = glm::vec3(0.0f);
	float m_yaw = 0.0f;
	float m_pitch = 0.0f;

	float m_fov = 60.0f;
	float m_aspect = 16.0f / 9.0f;
	float m_speed = 8.0f;
	float m_near = 0.001f;
	float m_far = 10000.0f;

};
