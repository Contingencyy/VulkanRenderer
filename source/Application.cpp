#include "Application.h"
#include "renderer/Renderer.h"
#include "Logger.h"
#include "Assets.h"
#include "Input.h"

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

		glm::mat4 view = glm::identity<glm::mat4>();
		glm::mat4 proj = glm::identity<glm::mat4>();
		float camera_speed = 10.0f;

		std::chrono::duration<float> delta_time = std::chrono::duration<float>(0.0f);
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

	static void SubmitModelNode(const Assets::Model& model, const Assets::Model::Node& node, const glm::mat4& node_transform)
	{
		for (size_t i = 0; i < node.mesh_handles.size(); ++i)
		{
			Renderer::SubmitMesh(node.mesh_handles[i], node.material_handles[i], node_transform);
		}

		for (size_t i = 0; i < node.children.size(); ++i)
		{
			const Assets::Model::Node& child_node = model.nodes[node.children[i]];
			glm::mat4 child_transform = node_transform * child_node.transform;
			SubmitModelNode(model, child_node, child_transform);
		}
	}

	static void SubmitModel(const Assets::Model& model, const glm::mat4& transform)
	{
		for (size_t i = 0; i < model.root_nodes.size(); ++i)
		{
			const Assets::Model::Node& root_node = model.nodes[model.root_nodes[i]];
			glm::mat4 root_transform = transform * root_node.transform;
			SubmitModelNode(model, root_node, root_transform);
		}
	}

	static void UpdateCamera(float delta_time)
	{
		// Make camera transform, and construct right/up/forward vectors from camera transform
		glm::mat4 camera_transform = glm::inverse(data.view);
		glm::vec3 right = glm::normalize(glm::vec3(camera_transform[0][0], camera_transform[0][1], camera_transform[0][2]));
		glm::vec3 up = glm::normalize(glm::vec3(camera_transform[1][0], camera_transform[1][1], camera_transform[1][2]));
		glm::vec3 forward = glm::normalize(glm::vec3(camera_transform[2][0], camera_transform[2][1], camera_transform[2][2]));

		// Translation
		static glm::vec3 translation(0.0f);
		translation += right * delta_time * data.camera_speed * Input::GetInputAxis1D(Input::Key_D, Input::Key_A);
		translation += up * delta_time * data.camera_speed * Input::GetInputAxis1D(Input::Key_Space, Input::Key_Shift);
		translation += forward * delta_time * data.camera_speed * Input::GetInputAxis1D(Input::Key_S, Input::Key_W);

		// Rotation
		int cursor_mode = glfwGetInputMode(data.window, GLFW_CURSOR);
		static glm::vec3 rotation;
		static float yaw = 0.0f, pitch = 0.0f;

		if (cursor_mode == GLFW_CURSOR_DISABLED)
		{
			double mouse_x, mouse_y;
			Input::GetMousePositionRel(mouse_x, mouse_y);

			float yaw_sign = camera_transform[1][1] < 0.0f ? -1.0f : 1.0f;
			rotation.y -= 0.001f * yaw_sign * mouse_x;
			rotation.x -= 0.001f * mouse_y;
			rotation.x = std::min(rotation.x, glm::radians(90.0f));
			rotation.x = std::max(rotation.x, glm::radians(-90.0f));
		}

		// Make view projection matrices
		glm::mat4 translation_matrix = glm::translate(glm::identity<glm::mat4>(), translation);
		glm::mat4 rotation_matrix = glm::mat4_cast(glm::quat(rotation));

		data.view = translation_matrix * rotation_matrix;
		data.view = glm::inverse(data.view);

		int width, height;
		glfwGetFramebufferSize(data.window, &width, &height);
		data.proj = glm::perspective(glm::radians(60.0f), (float)width / height, 0.001f, 1000.0f);
		data.proj[1][1] *= -1.0f;
	}

	void Init()
	{
		CreateWindow();

		Input::Init();
		Renderer::Init(data.window);

		Assets::Init();
		Assets::LoadGLTF("assets/models/gltf/ABeautifulGame/ABeautifulGame.gltf", "car");
		//Assets::LoadGLTF("assets/models/gltf/bmw_m6_rigged/scene.gltf", "car");

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
		UpdateCamera(dt);
		Input::Update();

		// NOTE: Temporary test setup, we should have a proper scene soon
		Assets::Model car = Assets::GetModel("car");
		glm::mat4 transform = glm::scale(glm::identity<glm::mat4>(), glm::vec3(10.0f));
		SubmitModel(car, transform);
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
		Renderer::BeginFrame(data.view, data.proj);
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
