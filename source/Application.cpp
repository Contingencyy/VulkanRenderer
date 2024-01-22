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
		if (width > 0 && height > 0)
		{
			data.active_scene.GetActiveCamera().OnResolutionChanged(width, height);
		}
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

	static void SpawnModelNodeEntity(const Assets::Model* model, const Assets::Model::Node& node, const glm::mat4& node_transform)
	{
		for (uint32_t i = 0; i < node.mesh_handles.size(); ++i)
		{
			data.active_scene.AddEntity<MeshObject>(node.mesh_handles[i], node.materials[i], node_transform, node.name);
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
		//Assets::LoadTexture("assets/textures/hdr/Env_Plaza.hdr", "Env", TEXTURE_FORMAT_RGBA32_SFLOAT, true, true);
		//Assets::LoadTexture("assets/textures/hdr/Env_Rocky_Hills.hdr", "Env", TEXTURE_FORMAT_RGBA32_SFLOAT, true, true);
		Assets::LoadTexture("assets/textures/hdr/Env_Victorian_Hall.hdr", "Env", TEXTURE_FORMAT_RGBA32_SFLOAT, true, true);
		//Assets::LoadGLTF("assets/models/gltf/ClearCoatToyCar/ToyCar.gltf", "model");
		//Assets::LoadGLTF("assets/models/gltf/ClearCoatTest/ClearCoatTest.gltf", "model");
		//Assets::LoadGLTF("assets/models/gltf/ClearCoatRing/ClearCoatRing.gltf", "model");
		//Assets::LoadGLTF("assets/models/gltf/ClearCoatSphere/ClearCoatSphere.gltf", "model");
		//Assets::LoadGLTF("assets/models/gltf/SponzaOld/Sponza.gltf", "model");
		//Assets::LoadGLTF("assets/models/gltf/Sponza/NewSponza_Main_glTF_002.gltf", "model");
		//Assets::LoadGLTF("assets/models/gltf/ClearCoatCarPaint/ClearCoatCarPaint.gltf", "model");
		Assets::LoadGLTF("assets/models/gltf/MetalRoughSpheres/MetalRoughSpheres.gltf", "model");
		//Assets::LoadGLTF("assets/models/gltf/ABeautifulGame/ABeautifulGame.gltf", "model");
		//Assets::LoadGLTF("assets/models/gltf/BMW_M6/scene.gltf", "model");
		//Assets::LoadGLTF("assets/models/gltf/BMW_M8/scene.gltf", "model");
		//Assets::LoadGLTF("assets/models/gltf/Lexus_Lc_500/scene.gltf", "model");

		glm::mat4 transform = glm::scale(glm::identity<glm::mat4>(), glm::vec3(10.0f));
		SpawnModelEntity("model", transform);

		data.active_scene.AddEntity<Pointlight>(glm::vec3(50.0f, 50.0f, -50.0f), glm::vec3(1.0f), 25.0f, "Pointlight1");
		data.active_scene.AddEntity<Pointlight>(glm::vec3(-50.0f, 50.0f, 50.0f), glm::vec3(1.0f), 25.0f, "Pointlight2");

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
		data.active_scene.RenderUI();
		Renderer::RenderUI();

		ImGui::Begin("General");
		float delta_time_ms = data.delta_time.count() * 1000.0f;

		ImGui::Text("FPS: %u", (uint32_t)(1000.0f / delta_time_ms));
		ImGui::Text("Frametime: %.3fms", delta_time_ms);
		ImGui::End();
	}

	static void Render()
	{
		Renderer::BeginFrameInfo frame_info = {};
		frame_info.view = data.active_scene.GetActiveCamera().GetView();
		frame_info.proj = data.active_scene.GetActiveCamera().GetProjection();
		frame_info.skybox_texture_handle = Assets::GetTexture("Env");
		Renderer::BeginFrame(frame_info);

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
