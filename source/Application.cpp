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

	static void SubmitModelNode(const Assets::Model& model, const Assets::Model::Node& node, const glm::mat4& node_transform)
	{
		for (size_t i = 0; i < node.mesh_handles.size(); ++i)
		{
			Renderer::SubmitMesh(node.mesh_handles[i], node.texture_handles[i], node_transform);
		}

		for (size_t i = 0; i < node.children.size(); ++i)
		{
			const Assets::Model::Node& child_node = model.nodes[node.children[i]];
			glm::mat4 child_transform = child_node.transform * node_transform;
			SubmitModelNode(model, child_node, child_transform);
		}
	}

	static void SubmitModel(const Assets::Model& model, const glm::mat4& transform)
	{
		for (size_t i = 0; i < model.root_nodes.size(); ++i)
		{
			const Assets::Model::Node& root_node = model.nodes[model.root_nodes[i]];
			glm::mat4 root_transform = root_node.transform * transform;
			SubmitModelNode(model, root_node, root_transform);
		}
	}

	void Init()
	{
		CreateWindow();
		Renderer::Init(data.window);

		Assets::Init();
		Assets::LoadTexture("assets/textures/statue.jpg", "statue");
		Assets::LoadGLTF("assets/models/gltf/toyota_chaser_tourerv/scene.gltf", "lambo_venevo");

		data.is_running = true;
	}

	void Exit()
	{
		data.is_running = false;

		Assets::Exit();

		Renderer::Exit();
		DestroyWindow();
	}

	void Run()
	{
		while (!data.should_close)
		{
			PollEvents();

			Renderer::BeginFrame();

			// NOTE: Temporary test setup, we should have a proper scene soon
			Assets::Model lambo_venevo = Assets::GetModel("lambo_venevo");
			glm::mat4 transform = glm::scale(glm::identity<glm::mat4>(), glm::vec3(0.01f));
			SubmitModel(lambo_venevo, transform);
			Renderer::RenderFrame();
			Renderer::EndFrame();
		}
	}

	bool ShouldClose()
	{
		return data.should_close;
	}

}
