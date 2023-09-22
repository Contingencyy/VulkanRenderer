#pragma once

typedef struct GLFWwindow;

namespace Renderer
{

	void Init(GLFWwindow* window);
	void Exit();

	void RenderFrame();

}
