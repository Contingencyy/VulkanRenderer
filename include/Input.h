#pragma once

struct GLFWwindow;

namespace Input
{

	enum Key
	{
		Key_W, Key_A, Key_S, Key_D,
		Key_Shift, Key_Space,
		Key_F1, Key_F2, Key_F3, Key_F4, Key_F5, Key_F6, Key_F7, Key_F8, Key_F9, Key_F10, Key_F11, Key_F12,
		NumKeys
	};

	enum Button
	{
		Button_LeftMouse, Button_RightMouse,
		NumButtons
	};

	enum KeyState
	{
		KeyState_Pressed, KeyState_Repeat, KeyState_Released
	};

	void GLFWKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
	void GLFWMouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
	void GLFWCursorPosCallback(GLFWwindow* window, double xpos, double ypos);
	void GLTFMouseScrollCollback(GLFWwindow* window, double xoffset, double yoffset);

	void Init(GLFWwindow* window);
	void Exit();
	void Update();

	bool IsKeyPressed(Key key, bool consume = false);
	bool IsKeyRepeated(Key key, uint32_t& num_repeats);
	bool IsButtonPressed(Button button, bool consume = false);
	bool IsButtonRepeated(Button button, uint32_t& num_repeats);

	float GetInputAxis1D(Key axis_pos, Key axis_neg);
	
	void GetMousePositionAbs(double& x, double& y);
	void GetMousePositionRel(double& x, double& y);

	void GetScrollAbs(double& x, double& y);
	void GetScrollRel(double& x, double& y);

	bool IsCursorDisabled();

}
