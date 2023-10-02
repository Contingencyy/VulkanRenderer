#include "Input.h"

#include "GLFW/glfw3.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"

#include <vector>
#include <unordered_map>

namespace Input
{

	struct Data
	{
		std::unordered_map<int, Key> key_mappings =
		{
			{ GLFW_KEY_W, Key::Key_W },
			{ GLFW_KEY_A, Key::Key_A },
			{ GLFW_KEY_S, Key::Key_S },
			{ GLFW_KEY_D, Key::Key_D },
			{ GLFW_KEY_SPACE, Key::Key_Space },
			{ GLFW_KEY_LEFT_SHIFT, Key::Key_Shift },
		};
		std::unordered_map<Key, bool> key_states;

		std::unordered_map<int, Button> button_mappings =
		{
			{ GLFW_MOUSE_BUTTON_LEFT, Button::Button_LeftMouse },
			{ GLFW_MOUSE_BUTTON_RIGHT, Button::Button_RightMouse },
		};
		std::unordered_map<Button, bool> button_states;

		struct MousePosition
		{
			double x = 0;
			double y = 0;
		};

		MousePosition mouse_pos_prev;
		MousePosition mouse_pos_curr;
	} static data;

	void GLFWKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
	{
		if (ImGui::GetIO().WantCaptureKeyboard)
		{
			return;
		}

		if (data.key_mappings.find(key) != data.key_mappings.end())
		{
			Key mapped_key = data.key_mappings.at(key);
			data.key_states.at(mapped_key) = action;
		}
	}

	void GLFWMouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
	{
		if (ImGui::GetIO().WantCaptureMouse)
		{
			return;
		}

		if (data.button_mappings.find(button) != data.button_mappings.end())
		{
			Button mapped_button = data.button_mappings.at(button);
			data.button_states.at(mapped_button) = action;
		}
	}

	void GLFWCursorPosCallback(GLFWwindow* window, double xpos, double ypos)
	{
		if (ImGui::GetIO().WantCaptureMouse)
		{
			return;
		}

		data.mouse_pos_prev = data.mouse_pos_curr;
		data.mouse_pos_curr.x = xpos;
		data.mouse_pos_curr.y = ypos;
	}

	void Init()
	{
		for (size_t i = 0; i < Key::NumKeys; ++i)
		{
			data.key_states.emplace((Key)i, false);
		}

		for (size_t i = 0; i < Button::NumButtons; ++i)
		{
			data.button_states.emplace((Button)i, false);
		}
	}

	void Exit()
	{
	}

	void Update()
	{
		data.mouse_pos_prev = data.mouse_pos_curr;
	}

	bool IsKeyPressed(Key key)
	{
		return data.key_states.at(key);
	}

	bool IsButtonPressed(Button button)
	{
		return data.button_states.at(button);
	}

	float GetInputAxis1D(Key axis_pos, Key axis_neg)
	{
		return (float)data.key_states[axis_pos] + (-data.key_states[axis_neg]);
	}

	void GetMousePositionAbs(double& x, double& y)
	{
		x = data.mouse_pos_curr.x;
		y = data.mouse_pos_curr.y;
	}

	void GetMousePositionRel(double& x, double& y)
	{
		x = data.mouse_pos_curr.x - data.mouse_pos_prev.x;
		y = data.mouse_pos_curr.y - data.mouse_pos_prev.y;
	}

}
