#include "Precomp.h"
#include "Camera.h"
#include "Input.h"

Camera::Camera(const glm::vec3& pos, const glm::vec3& view_dir, float vfov)
	: m_vfov(vfov)
{
	m_view = glm::lookAtRH(pos, pos + view_dir, glm::vec3(0.0f, 1.0f, 0.0f));
}

void Camera::Update(float dt)
{
	// Make camera transform, and construct right/up/forward vectors from camera transform
	glm::mat4 camera_transform = glm::inverse(m_view);
	glm::vec3 right = glm::normalize(glm::vec3(camera_transform[0][0], camera_transform[0][1], camera_transform[0][2]));
	glm::vec3 up = glm::normalize(glm::vec3(camera_transform[1][0], camera_transform[1][1], camera_transform[1][2]));
	glm::vec3 forward = glm::normalize(glm::vec3(camera_transform[2][0], camera_transform[2][1], camera_transform[2][2]));

	// Translation
	m_translation += right * dt * m_speed * Input::GetInputAxis1D(Input::Key_D, Input::Key_A);
	m_translation += up * dt * m_speed * Input::GetInputAxis1D(Input::Key_Space, Input::Key_Shift);
	m_translation += forward * dt * m_speed * Input::GetInputAxis1D(Input::Key_S, Input::Key_W);

	// Rotation
	if (Input::IsCursorDisabled())
	{
		double mouse_x, mouse_y;
		Input::GetMousePositionRel(mouse_x, mouse_y);

		float yaw_sign = camera_transform[1][1] < 0.0f ? -1.0f : 1.0f;
		m_rotation.y -= 0.001f * yaw_sign * mouse_x;
		m_rotation.x -= 0.001f * mouse_y;
		m_rotation.x = std::min(m_rotation.x, glm::radians(90.0f));
		m_rotation.x = std::max(m_rotation.x, glm::radians(-90.0f));
	}

	// Scroll wheel speed
	if (Input::IsCursorDisabled())
	{
		double scroll_x, scroll_y;
		Input::GetScrollRel(scroll_x, scroll_y);

		m_speed += scroll_y * std::max(std::sqrtf(m_speed), 0.01f);
		m_speed = std::max(m_speed, 0.0f);
	}

	// Make view projection matrices
	glm::mat4 translation_matrix = glm::translate(glm::identity<glm::mat4>(), m_translation);
	glm::mat4 rotation_matrix = glm::mat4_cast(glm::quat(m_rotation));

	m_view = translation_matrix * rotation_matrix;
	m_view = glm::inverse(m_view);
}

glm::mat4 Camera::GetView() const
{
	return m_view;
}

float Camera::GetVerticalFOV() const
{
	return m_vfov;
}
