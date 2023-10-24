#pragma once

struct GLFWwindow;

namespace Input
{

	enum Key
	{
		Key_W, Key_A, Key_S, Key_D,
		Key_Shift, Key_Space,
		NumKeys
	};

	enum Button
	{
		Button_LeftMouse, Button_RightMouse,
		NumButtons
	};

	void GLFWKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
	void GLFWMouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
	void GLFWCursorPosCallback(GLFWwindow* window, double xpos, double ypos);

	void Init(GLFWwindow* window);
	void Exit();
	void Update();

	bool IsKeyPressed(Key key);
	bool IsButtonPressed(Button button);

	float GetInputAxis1D(Key axis_pos, Key axis_neg);
	
	void GetMousePositionAbs(double& x, double& y);
	void GetMousePositionRel(double& x, double& y);

	bool IsCursorDisabled();

}
