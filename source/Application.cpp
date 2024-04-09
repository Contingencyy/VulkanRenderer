#include "Precomp.h"
#include "Application.h"
#include "renderer/Renderer.h"
#include "Logger.h"
#include "assets/AssetManager.h"
#include "Input.h"
#include "Scene.h"

#include "GLFW/glfw3.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"

namespace Application
{

	static bool is_running = false;
	static bool should_close = false;

	struct Data
	{
		GLFWwindow* window = nullptr;
		uint32_t window_width = 0;
		uint32_t window_height = 0;

		bool window_focused = true;
		bool render_ui = true;

		std::chrono::duration<float> delta_time = std::chrono::duration<float>(0.0f);

		Scene active_scene;

		AssetHandle tex_kermit;
		AssetHandle tex_hdr;
		AssetHandle sponza_mesh;
		AssetHandle model_mesh;
	} static *data;

	const uint32_t DEFAULT_WINDOW_WIDTH = 1280;
	const uint32_t DEFAULT_WINDOW_HEIGHT = 720;

	static void FramebufferResizeCallback(GLFWwindow* window, int width, int height)
	{
		data->window_width = static_cast<uint32_t>(width);
		data->window_height = static_cast<uint32_t>(height);
	}

	static void CreateWindow()
	{
		glfwInit();
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		data->window = glfwCreateWindow(DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT, "VulkanRenderer", nullptr, nullptr);
		glfwSetFramebufferSizeCallback(data->window, FramebufferResizeCallback);

		glfwSetKeyCallback(data->window, Input::GLFWKeyCallback);
		glfwSetMouseButtonCallback(data->window, Input::GLFWMouseButtonCallback);
		glfwSetCursorPosCallback(data->window, Input::GLFWCursorPosCallback);
		glfwSetScrollCallback(data->window, Input::GLTFMouseScrollCollback);
		glfwSetInputMode(data->window, GLFW_LOCK_KEY_MODS, GLFW_TRUE);

		int32_t width = 0, height = 0;
		glfwGetFramebufferSize(data->window, &width, &height);

		data->window_width = static_cast<uint32_t>(width);
		data->window_height = static_cast<uint32_t>(height);
	}

	static void DestroyWindow()
	{
		glfwDestroyWindow(data->window);
		glfwTerminate();
	}

	static void PollEvents()
	{
		glfwPollEvents();
		if (glfwWindowShouldClose(data->window))
		{
			should_close = true;
		}

		if (Input::IsButtonPressed(Input::Button_LeftMouse))
		{
			glfwSetInputMode(data->window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		}
		if (Input::IsButtonPressed(Input::Button_RightMouse))
		{
			glfwSetInputMode(data->window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		}
	}

	static void SpawnModelNodeEntity(ModelAsset* model_asset, const ModelAsset::Node& node, const glm::mat4& node_transform)
	{
		for (uint32_t i = 0; i < node.mesh_render_handles.size(); ++i)
		{
			data->active_scene.AddEntity<MeshObject>(node.mesh_render_handles[i], node.materials[i], node_transform, node.mesh_names[i]);
		}

		for (uint32_t i = 0; i < node.children.size(); ++i)
		{
			const ModelAsset::Node& child_node = model_asset->nodes[node.children[i]];
			glm::mat4 child_transform = node_transform * child_node.transform;

			SpawnModelNodeEntity(model_asset, child_node, child_transform);
		}
	}

	static void SpawnModelEntity(AssetHandle model_handle, const glm::mat4& transform)
	{
		ModelAsset* model_asset = AssetManager::GetAsset<ModelAsset>(model_handle);
		if (!model_asset)
			return;

		for (uint32_t i = 0; i < model_asset->root_nodes.size(); ++i)
		{
			const ModelAsset::Node& root_node = model_asset->nodes[model_asset->root_nodes[i]];
			glm::mat4 root_transform = transform * root_node.transform;
		
			SpawnModelNodeEntity(model_asset, root_node, root_transform);
		}
	}

	void Init()
	{
		data = new Data();

		CreateWindow();

		Input::Init(data->window);
		Renderer::Init(data->window, data->window_width, data->window_height);

		AssetManager::Init("assets");
		data->tex_kermit = AssetManager::ImportTexture("assets\\textures\\kermit.png", TEXTURE_FORMAT_RGBA8_UNORM, true, false);
		
		data->tex_hdr = AssetManager::ImportTexture("assets\\textures\\hdr\\Env_Golden_Bay.hdr", TEXTURE_FORMAT_RGBA32_SFLOAT, true, true);

		data->sponza_mesh = AssetManager::ImportModel("assets\\models\\gltf\\SponzaOld\\Sponza.gltf");
		data->model_mesh = AssetManager::ImportModel("assets\\models\\gltf\\ClearCoatSphere\\ClearcoatSphere.gltf");

		glm::mat4 transform = glm::scale(glm::identity<glm::mat4>(), glm::vec3(1.0f));
		SpawnModelEntity(data->sponza_mesh, transform);
		transform = glm::scale(glm::translate(glm::identity<glm::mat4>(), glm::vec3(-2.5f, 1.25f, -0.25f)), glm::vec3(1.0f));
		SpawnModelEntity(data->model_mesh, transform);

		glm::mat4 area_light_transform = glm::translate(glm::identity<glm::mat4>(), glm::vec3(7.0f, 1.25f, -0.25f));
		area_light_transform = glm::rotate(area_light_transform, glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		area_light_transform = glm::scale(area_light_transform, glm::vec3(2.5f, 1.5f, 1.0f));
		data->active_scene.AddEntity<AreaLight>(AssetManager::GetAsset<TextureAsset>(data->tex_kermit)->texture_render_handle, area_light_transform, glm::vec3(1.0f, 0.95f, 0.8f), 5.0f, true, "AreaLight0");

		area_light_transform = glm::translate(glm::identity<glm::mat4>(), glm::vec3(-8.0f, 1.25f, -0.25f));
		area_light_transform = glm::rotate(area_light_transform, glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		area_light_transform = glm::scale(area_light_transform, glm::vec3(2.5f, 1.5f, 1.0f));
		data->active_scene.AddEntity<AreaLight>(RenderResourceHandle(), area_light_transform, glm::vec3(1.0f, 0.95f, 0.8f), 5.0f, true, "AreaLight1");

		is_running = true;
	}

	void Exit()
	{
		is_running = false;

		AssetManager::Exit();
		Renderer::Exit();
		Input::Exit();

		DestroyWindow();

		delete data;
		data = nullptr;
	}

	static void Update(float dt)
	{
		data->active_scene.Update(dt);
		Input::Update();
	}

	static void RenderUI()
	{
		if (Input::IsKeyPressed(Input::Key_F1, true))
		{
			data->render_ui = !data->render_ui;
		}

		if (data->render_ui)
		{
			data->active_scene.RenderUI();
			AssetManager::RenderUI();
			Renderer::RenderUI();

			if (ImGui::Begin("Application", nullptr, ImGuiWindowFlags_MenuBar))
			{
				if (ImGui::BeginMenuBar())
				{
					if (ImGui::BeginMenu("File"))
					{
						if (ImGui::MenuItem("Restart"))
						{
							is_running = false;
						}
						if (ImGui::MenuItem("Exit"))
						{
							should_close = true;
						}
						ImGui::EndMenu();
					}
					ImGui::EndMenuBar();
				}

				float delta_time_ms = data->delta_time.count() * 1000.0f;
				ImGui::Text("FPS: %u", (uint32_t)(1000.0f / delta_time_ms));
				ImGui::Text("Frametime: %.3fms", delta_time_ms);
			}
			ImGui::End();
		}
	}

	static void Render()
	{
		Renderer::BeginFrameInfo frame_info = {};
		frame_info.camera_view = data->active_scene.GetActiveCamera().GetView();
		frame_info.camera_vfov = data->active_scene.GetActiveCamera().GetVerticalFOV();
		frame_info.skybox_texture_handle = AssetManager::GetAsset<TextureAsset>(data->tex_hdr)->texture_render_handle;
		Renderer::BeginFrame(frame_info);

		data->active_scene.Render();

		Renderer::RenderFrame();
		RenderUI();
		Renderer::EndFrame();
	}

	void Run()
	{
		std::chrono::high_resolution_clock high_res_clock = {};
		std::chrono::high_resolution_clock::time_point curr_time = high_res_clock.now();
		std::chrono::high_resolution_clock::time_point prev_time = high_res_clock.now();

		while (is_running && !should_close)
		{
			curr_time = high_res_clock.now();
			data->delta_time = curr_time - prev_time;

			PollEvents();
			Update(data->delta_time.count());
			Render();

			prev_time = curr_time;
		}
	}

	bool ShouldClose()
	{
		return should_close;
	}

}
