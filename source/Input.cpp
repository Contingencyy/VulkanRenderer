#include "Precomp.h"
#include "Input.h"

#include "GLFW/glfw3.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"

namespace Input
{

	struct Data
	{
		GLFWwindow* window;

		struct State
		{
			KeyState state;
			uint32_t num_repeats;
			bool consumed;
		};

		std::unordered_map<int, Key> key_mappings =
		{
			{ GLFW_KEY_W, Key::Key_W },
			{ GLFW_KEY_A, Key::Key_A },
			{ GLFW_KEY_S, Key::Key_S },
			{ GLFW_KEY_D, Key::Key_D },
			{ GLFW_KEY_SPACE, Key::Key_Space },
			{ GLFW_KEY_LEFT_SHIFT, Key::Key_Shift },
			{ GLFW_KEY_F1, Key::Key_F1 },
			{ GLFW_KEY_F2, Key::Key_F2 },
			{ GLFW_KEY_F3, Key::Key_F3 },
			{ GLFW_KEY_F4, Key::Key_F4 },
			{ GLFW_KEY_F5, Key::Key_F5 },
			{ GLFW_KEY_F6, Key::Key_F6 },
			{ GLFW_KEY_F7, Key::Key_F7 },
			{ GLFW_KEY_F8, Key::Key_F8 },
			{ GLFW_KEY_F9, Key::Key_F9 },
			{ GLFW_KEY_F10, Key::Key_F10 },
			{ GLFW_KEY_F11, Key::Key_F11 },
			{ GLFW_KEY_F12, Key::Key_F12 },
		};
		std::unordered_map<Key, State> key_states;

		std::unordered_map<int, Button> button_mappings =
		{
			{ GLFW_MOUSE_BUTTON_LEFT, Button::Button_LeftMouse },
			{ GLFW_MOUSE_BUTTON_RIGHT, Button::Button_RightMouse },
		};
		std::unordered_map<Button, State> button_states;

		std::unordered_map<int, KeyState> action_mappings =
		{
			{ GLFW_RELEASE, KeyState::KeyState_Released },
			{ GLFW_PRESS, KeyState::KeyState_Pressed },
			{ GLFW_REPEAT, KeyState::KeyState_Repeat }
		};

		struct MousePosition
		{
			double x = 0;
			double y = 0;
		};

		MousePosition mouse_pos_prev;
		MousePosition mouse_pos_curr;

		MousePosition scroll_prev;
		MousePosition scroll_curr;
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
			Data::State& key_state = data.key_states.at(mapped_key);

			key_state.state = data.action_mappings.at(action);
			if (key_state.state == KeyState::KeyState_Released)
			{
				key_state.num_repeats = 0;
			}
			else if (key_state.state == KeyState_Pressed)
			{
				key_state.consumed = false;
			}
			else if (key_state.state == KeyState::KeyState_Repeat)
			{
				key_state.num_repeats++;
			}
		}

		const char* action_labels[] = { "Released", "Pressed", "Repeat" };
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
			Data::State& button_state = data.button_states.at(mapped_button);
			
			button_state.state = data.action_mappings.at(action);
			if (button_state.state == KeyState::KeyState_Released)
			{
				button_state.num_repeats = 0;
			}
			else if (button_state.state == KeyState_Pressed)
			{
				button_state.consumed = false;
			}
			else if (button_state.state == KeyState::KeyState_Repeat)
			{
				button_state.num_repeats++;
			}
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

	void GLTFMouseScrollCollback(GLFWwindow* window, double xoffset, double yoffset)
	{
		if (ImGui::GetIO().WantCaptureMouse)
		{
			return;
		}

		data.scroll_prev = data.scroll_curr;
		data.scroll_curr.x += xoffset;
		data.scroll_curr.y += yoffset;
	}

	void Init(GLFWwindow* window)
	{
		data.window = window;

		for (size_t i = 0; i < Key::NumKeys; ++i)
		{
			data.key_states.emplace((Key)i, Data::State(KeyState::KeyState_Released, 0));
		}

		for (size_t i = 0; i < Button::NumButtons; ++i)
		{
			data.button_states.emplace((Button)i, Data::State(KeyState::KeyState_Released, 0));
		}
	}

	void Exit()
	{
	}

	void Update()
	{
		data.mouse_pos_prev = data.mouse_pos_curr;
		data.scroll_prev = data.scroll_curr;
	}

	bool IsKeyPressed(Key key, bool consume)
	{
		Data::State& key_state = data.key_states.at(key);
		if (key_state.consumed)
			return false;

		key_state.consumed = consume;
		return key_state.state == KeyState::KeyState_Pressed;
	}

	bool IsKeyRepeated(Key key, uint32_t& num_repeats)
	{
		num_repeats = data.key_states.at(key).num_repeats;
		return data.key_states.at(key).state == KeyState::KeyState_Repeat;
	}

	bool IsButtonPressed(Button button, bool consume)
	{
		Data::State& button_state = data.button_states.at(button);
		if (button_state.consumed)
			return false;

		button_state.consumed = consume;
		return button_state.state == KeyState::KeyState_Pressed;
	}

	bool IsButtonRepeated(Button button, uint32_t& num_repeats)
	{
		num_repeats = data.button_states.at(button).num_repeats;
		return data.button_states.at(button).state == KeyState::KeyState_Repeat;
	}

	float GetInputAxis1D(Key axis_pos, Key axis_neg)
	{
		bool pos_state = data.key_states[axis_pos].state != KeyState_Released;
		bool neg_state = data.key_states[axis_neg].state != KeyState_Released;
		return (float)pos_state + (-neg_state);
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

	void GetScrollAbs(double& x, double& y)
	{
		x = data.scroll_curr.x;
		y = data.scroll_prev.y;
	}

	void GetScrollRel(double& x, double& y)
	{
		x = data.scroll_curr.x - data.scroll_prev.x;
		y = data.scroll_curr.y - data.scroll_prev.y;
	}

	bool IsCursorDisabled()
	{
		int cursor_mode = glfwGetInputMode(data.window, GLFW_CURSOR);
		return cursor_mode == GLFW_CURSOR_DISABLED;
	}

}
