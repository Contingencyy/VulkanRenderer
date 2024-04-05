#include "Precomp.h"
#include "Application.h"
#include "renderer/Renderer.h"
#include "Logger.h"
#include "Assets.h"
#include "Input.h"
#include "Scene.h"

#include "GLFW/glfw3.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"

#include <filesystem>

namespace Application
{

	struct Data
	{
		GLFWwindow* window;
		uint32_t window_width = 0;
		uint32_t window_height = 0;

		bool is_running = false;
		bool should_close = false;
		bool window_focused = true;
		bool render_ui = true;

		std::chrono::duration<float> delta_time = std::chrono::duration<float>(0.0f);

		Scene active_scene;
	} static data;

	const uint32_t DEFAULT_WINDOW_WIDTH = 1280;
	const uint32_t DEFAULT_WINDOW_HEIGHT = 720;

	static void FramebufferResizeCallback(GLFWwindow* window, int width, int height)
	{
		data.window_width = static_cast<uint32_t>(width);
		data.window_height = static_cast<uint32_t>(height);
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
		glfwSetScrollCallback(data.window, Input::GLTFMouseScrollCollback);
		glfwSetInputMode(data.window, GLFW_LOCK_KEY_MODS, GLFW_TRUE);

		int32_t width = 0, height = 0;
		glfwGetFramebufferSize(data.window, &width, &height);

		data.window_width = static_cast<uint32_t>(width);
		data.window_height = static_cast<uint32_t>(height);
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
			data.active_scene.AddEntity<MeshObject>(node.mesh_handles[i], node.materials[i], node_transform, node.mesh_names[i]);
		}

		for (uint32_t i = 0; i < node.children.size(); ++i)
		{
			const Assets::Model::Node& child_node = model->nodes[node.children[i]];
			glm::mat4 child_transform = node_transform * child_node.transform;

			SpawnModelNodeEntity(model, child_node, child_transform);
		}
	}

	static void SpawnModelEntity(const std::filesystem::path& filepath, const glm::mat4& transform)
	{
		Assets::Model* model = Assets::GetModel(filepath);
		if (!model)
		{
			LOG_ERR("Assets", "Failed to fetch model with name {}", filepath.string());
			return;
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
		Renderer::Init(data.window, data.window_width, data.window_height);

		Assets::Init("assets\\");
		Assets::LoadTexture("assets\\textures\\kermit.png", TEXTURE_FORMAT_RGBA8_UNORM, true, false);

		Assets::LoadTexture("assets\\textures\\hdr\\Env_Plaza.hdr", TEXTURE_FORMAT_RGBA32_SFLOAT, true, true);

		Assets::LoadGLTF("assets\\models\\gltf\\SponzaOld\\Sponza.gltf");
		Assets::LoadGLTF("assets\\models\\gltf\\ClearCoatSphere\\ClearcoatSphere.gltf");

		glm::mat4 transform = glm::scale(glm::identity<glm::mat4>(), glm::vec3(10.0f));
		SpawnModelEntity("assets\\models\\gltf\\SponzaOld\\Sponza.gltf", transform);
		SpawnModelEntity("assets\\models\\gltf\\ClearCoatSphere\\ClearcoatSphere.gltf", transform);

		glm::mat4 area_light_transform = glm::translate(glm::identity<glm::mat4>(), glm::vec3(70.0f, 10.0f, -3.0f));
		area_light_transform = glm::rotate(area_light_transform, glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		area_light_transform = glm::scale(area_light_transform, glm::vec3(12.0f, 8.0f, 1.0f));
		data.active_scene.AddEntity<AreaLight>(Assets::GetTexture("assets\\textures\\kermit.png"), area_light_transform, glm::vec3(1.0f, 0.95f, 0.8f), 5.0f, true, "AreaLight0");

		area_light_transform = glm::translate(glm::identity<glm::mat4>(), glm::vec3(-80.0f, 10.0f, -3.0f));
		area_light_transform = glm::rotate(area_light_transform, glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		area_light_transform = glm::scale(area_light_transform, glm::vec3(12.0f, 8.0f, 1.0f));
		data.active_scene.AddEntity<AreaLight>(TextureHandle_t(), area_light_transform, glm::vec3(1.0f, 0.95f, 0.8f), 5.0f, true, "AreaLight1");

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
		if (Input::IsKeyPressed(Input::Key_F1, true))
		{
			data.render_ui = !data.render_ui;
		}

		if (data.render_ui)
		{
			data.active_scene.RenderUI();
			Assets::RenderUI();
			Renderer::RenderUI();

			if (ImGui::Begin("General"))
			{
				float delta_time_ms = data.delta_time.count() * 1000.0f;
				ImGui::Text("FPS: %u", (uint32_t)(1000.0f / delta_time_ms));
				ImGui::Text("Frametime: %.3fms", delta_time_ms);
			}
			ImGui::End();
		}
	}

	static void Render()
	{
		Renderer::BeginFrameInfo frame_info = {};
		frame_info.camera_view = data.active_scene.GetActiveCamera().GetView();
		frame_info.camera_vfov = data.active_scene.GetActiveCamera().GetVerticalFOV();
		frame_info.skybox_texture_handle = Assets::GetTexture("assets\\textures\\hdr\\Env_Plaza.hdr");
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
