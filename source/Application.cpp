#include "Application.h"
#include "renderer/Renderer.h"
#include "Logger.h"
#include "Assets.h"
#include "Input.h"
#include "Scene.h"

#include "GLFW/glfw3.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"

#include <chrono>

namespace Application
{

	struct Data
	{
		GLFWwindow* window;

		bool is_running = false;
		bool should_close = false;
		bool window_focused = true;

		std::chrono::duration<float> delta_time = std::chrono::duration<float>(0.0f);

		Scene active_scene;
	} static data;

	const uint32_t DEFAULT_WINDOW_WIDTH = 1280;
	const uint32_t DEFAULT_WINDOW_HEIGHT = 720;

	static void FramebufferResizeCallback(GLFWwindow* window, int width, int height)
	{
		data.active_scene.GetActiveCamera().OnResolutionChanged(width, height);
	}

	static void CreateWindow()
	{
		glfwInit();
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		data.window = glfwCreateWindow(DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT, "VulkanRenderer", nullptr, nullptr);
		glfwSetFramebufferSizeCallback(data.window, FramebufferResizeCallback);

		glfwSetKeyCallback(data.window, Input::GLFWKeyCallback);
		glfwSetMouseButtonCallback(data.window, Input::GLFWMouseButtonCallback);
		glfwSetCursorPosCallback(data.window, Input::GLFWCursorPosCallback);
		glfwSetInputMode(data.window, GLFW_LOCK_KEY_MODS, GLFW_TRUE);
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

		if (Input::IsButtonPressed(Input::Button_LeftMouse))
		{
			glfwSetInputMode(data.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		}
		if (Input::IsButtonPressed(Input::Button_RightMouse))
		{
			glfwSetInputMode(data.window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		}
	}

	//static void SubmitModelNode(const Assets::Model& model, const Assets::Model::Node& node, const glm::mat4& node_transform)
	//{
	//	for (size_t i = 0; i < node.mesh_handles.size(); ++i)
	//	{
	//		Renderer::SubmitMesh(node.mesh_handles[i], node.material_handles[i], node_transform);
	//	}
	//
	//	for (size_t i = 0; i < node.children.size(); ++i)
	//	{
	//		const Assets::Model::Node& child_node = model.nodes[node.children[i]];
	//		glm::mat4 child_transform = node_transform * child_node.transform;
	//		SubmitModelNode(model, child_node, child_transform);
	//	}
	//}
	//
	//static void SubmitModel(const Assets::Model& model, const glm::mat4& transform)
	//{
	//	for (size_t i = 0; i < model.root_nodes.size(); ++i)
	//	{
	//		const Assets::Model::Node& root_node = model.nodes[model.root_nodes[i]];
	//		glm::mat4 root_transform = transform * root_node.transform;
	//		SubmitModelNode(model, root_node, root_transform);
	//	}
	//}

	static void SpawnModelNodeEntity(const Assets::Model* model, const Assets::Model::Node& node, const glm::mat4& node_transform)
	{
		for (uint32_t i = 0; i < node.mesh_handles.size(); ++i)
		{
			data.active_scene.AddEntity<MeshObject>(node.mesh_handles[i], node.material_handles[i], node_transform);
		}

		for (uint32_t i = 0; i < node.children.size(); ++i)
		{
			const Assets::Model::Node& child_node = model->nodes[node.children[i]];
			glm::mat4 child_transform = node_transform * child_node.transform;

			SpawnModelNodeEntity(model, child_node, child_transform);
		}
	}

	static void SpawnModelEntity(const char* model_name, const glm::mat4& transform)
	{
		Assets::Model* model = Assets::GetModel(model_name);
		if (!model)
		{
			VK_EXCEPT("Assets", "Failed to fetch model with name {}", model_name);
		}

		for (uint32_t i = 0; i < model->root_nodes.size(); ++i)
		{
			const Assets::Model::Node& root_node = model->nodes[model->root_nodes[i]];
			glm::mat4 root_transform = transform * root_node.transform;
		
			SpawnModelNodeEntity(model, root_node, root_transform);
		}
	}

	void Init()
	{
		CreateWindow();

		Input::Init(data.window);
		Renderer::Init(data.window);

		Assets::Init();
		Assets::LoadGLTF("assets/models/gltf/ABeautifulGame/ABeautifulGame.gltf", "car");
		//Assets::LoadGLTF("assets/models/gltf/bmw_m6_rigged/scene.gltf", "car");

		// NOTE: Temporary test setup, we should have a proper scene soon
		glm::mat4 transform = glm::scale(glm::identity<glm::mat4>(), glm::vec3(10.0f));
		SpawnModelEntity("car", transform);

		data.is_running = true;
	}

	void Exit()
	{
		data.is_running = false;

		Assets::Exit();
		Renderer::Exit();
		Input::Exit();

		DestroyWindow();
	}

	static void Update(float dt)
	{
		data.active_scene.Update(dt);
		Input::Update();
	}

	static void RenderUI()
	{
		Renderer::RenderUI();

		// NOTE: Temporary menu code
		ImGui::Begin("General");
		float delta_time_ms = data.delta_time.count() * 1000.0f;

		ImGui::Text("FPS: %u", (uint32_t)(1000.0f / delta_time_ms));
		ImGui::Text("Frametime: %.3fms", delta_time_ms);
		ImGui::End();
	}

	static void Render()
	{
		Renderer::BeginFrame(data.active_scene.GetActiveCamera().GetView(), data.active_scene.GetActiveCamera().GetProjection());

		data.active_scene.Render();

		Renderer::RenderFrame();
		RenderUI();
		Renderer::EndFrame();
	}

	void Run()
	{
		std::chrono::high_resolution_clock high_res_clock = {};
		std::chrono::high_resolution_clock::time_point curr_time = high_res_clock.now();
		std::chrono::high_resolution_clock::time_point prev_time = high_res_clock.now();

		while (!data.should_close)
		{
			curr_time = high_res_clock.now();
			data.delta_time = curr_time - prev_time;

			PollEvents();
			Update(data.delta_time.count());
			Render();

			prev_time = curr_time;
		}
	}

	bool ShouldClose()
	{
		return data.should_close;
	}

}
