#include "Application.h"
#include "renderer/Renderer.h"
#include "Logger.h"

#include "GLFW/glfw3.h"

namespace Application
{

	struct Data
	{
		GLFWwindow* window;

		bool framebuffer_resized = false;
		bool is_running = false;
		bool should_close = false;
	} static app;

	const uint32_t DEFAULT_WINDOW_WIDTH = 1280;
	const uint32_t DEFAULT_WINDOW_HEIGHT = 720;

	static void FramebufferResizeCallback(GLFWwindow* window, int width, int height)
	{
		app.framebuffer_resized = true;
	}

	static void CreateWindow()
	{
		glfwInit();
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		app.window = glfwCreateWindow(DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT, "VulkanRenderer", nullptr, nullptr);
		glfwSetFramebufferSizeCallback(app.window, FramebufferResizeCallback);
	}

	static void DestroyWindow()
	{
		glfwDestroyWindow(app.window);
		glfwTerminate();
	}

	static void PollEvents()
	{
		glfwPollEvents();
		if (glfwWindowShouldClose(app.window))
		{
			app.should_close = true;
		}
	}

	void Init()
	{
		CreateWindow();
		Renderer::Init(app.window);

		app.is_running = true;
	}

	void Exit()
	{
		app.is_running = false;

		Renderer::Exit();
		DestroyWindow();
	}

	void Run()
	{
		while (!app.should_close)
		{
			PollEvents();
			Renderer::RenderFrame();
		}
	}

	bool ShouldClose()
	{
		return app.should_close;
	}

}
