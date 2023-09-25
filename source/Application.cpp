#include "Application.h"
#include "renderer/Renderer.h"
#include "Logger.h"
#include "Assets.h"

#include "GLFW/glfw3.h"

namespace Application
{

	struct Data
	{
		GLFWwindow* window;

		bool is_running = false;
		bool should_close = false;
	} static data;

	const uint32_t DEFAULT_WINDOW_WIDTH = 1280;
	const uint32_t DEFAULT_WINDOW_HEIGHT = 720;

	static void FramebufferResizeCallback(GLFWwindow* window, int width, int height)
	{
	}

	static void CreateWindow()
	{
		glfwInit();
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		data.window = glfwCreateWindow(DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT, "VulkanRenderer", nullptr, nullptr);
		glfwSetFramebufferSizeCallback(data.window, FramebufferResizeCallback);
	}

	static void DestroyWindow()
	{
		glfwDestroyWindow(data.window);
		glfwTerminate();
	}

	static void PollEvents()
	{
		glfwPollEvents();
		if (glfwWindowShouldClose(data.window))
		{
			data.should_close = true;
		}
	}

	void Init()
	{
		CreateWindow();
		Renderer::Init(data.window);

		Assets::LoadTexture("assets/textures/statue.jpg", "statue");

		data.is_running = true;
	}

	void Exit()
	{
		data.is_running = false;

		Renderer::Exit();
		DestroyWindow();
	}

	void Run()
	{
		while (!data.should_close)
		{
			PollEvents();
			Renderer::RenderFrame();
		}
	}

	bool ShouldClose()
	{
		return data.should_close;
	}

}
