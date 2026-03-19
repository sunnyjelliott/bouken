#include "app.h"

#include <vk_mem_alloc.h>

#include "input.h"
#include "sceneloader.h"

namespace {
class UsdDiagnosticDelegate final : public TfDiagnosticMgr::Delegate {
   public:
	void IssueError(TfError const& err) override {
		std::cout << "[USD ERROR] " << err.GetCommentary() << "\n  in "
		          << err.GetSourceFunction() << " (" << err.GetSourceFileName()
		          << ":" << err.GetSourceLineNumber() << ")" << std::endl;
	}
	void IssueFatalError(TfCallContext const& ctx,
	                     std::string const& msg) override {
		std::cout << "[USD FATAL] " << msg << "\n  in " << ctx.GetFunction()
		          << " (" << ctx.GetFile() << ":" << ctx.GetLine() << ")"
		          << std::endl;
	}
	void IssueStatus(TfStatus const&) override {}
	void IssueWarning(TfWarning const& w) override {
		std::cout << "[USD WARN] " << w.GetCommentary() << std::endl;
	}
};
}  // namespace

void Application::run() {
	initWindow();
	initVulkan();
	initScene();
	mainLoop();
	cleanup();
}

void Application::initWindow() {
	glfwInit();

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(
	    GLFW_RESIZABLE,
	    GLFW_FALSE);  // TODO: Re-enable when window resizing is handled.

	m_window =
	    glfwCreateWindow(WIDTH, HEIGHT, "Bunny Engine", nullptr, nullptr);

	m_inputBackend = new GLFWInputBackend(m_window);

	Input::initialize(m_inputBackend);
	Input::setMouseCaptured(true);
}

void Application::initVulkan() {
	m_context.init(m_window);
	m_swapChain.init(m_context, m_window, WIDTH, HEIGHT);

	m_vulkanTextureBackend.initialize(m_context);
	m_textureManager.initialize(&m_vulkanTextureBackend);

	m_renderSystem.initialize(m_context, m_swapChain);
}

void Application::initScene() {
	static UsdDiagnosticDelegate usdDelegate;
	TfDiagnosticMgr::GetInstance().AddDelegate(&usdDelegate);

	// Camera
	m_activeCamera = m_world.createEntity();
	m_world.addComponent(m_activeCamera,
	                     Transform{.position = glm::vec3(0.0f, 2.0f, 8.0f)});
	m_world.addComponent(
	    m_activeCamera,
	    Camera{.fov = 45.0f, .nearPlane = 0.1f, .farPlane = 100.0f});
	m_cameraSystem.setActiveCamera(m_activeCamera);

	m_cameraSystem.setActiveCamera(m_activeCamera);

	SceneLoadOptions options;
	options.createHeirarchy = true;
	std::filesystem::path scenePath = std::filesystem::current_path() /
	                                  "assets" / "models" / "main_sponza" /
	                                  "sponza.usdc";

	SceneLoader::loadScene(scenePath.string(), m_world, m_renderSystem,
	                       m_textureManager, m_materialManager, options);
}

void Application::mainLoop() {
	while (!glfwWindowShouldClose(m_window)) {
		float currentTime = static_cast<float>(glfwGetTime());
		float deltaTime = currentTime - m_lastFrameTime;
		m_lastFrameTime = currentTime;

		glfwPollEvents();

		if (Input::isActionPressed(InputAction::ToggleMouseCapture)) {
			Input::setMouseCaptured(!Input::isMouseCaptured());
		}

		m_cameraSystem.updateFreeFly(
		    m_world, deltaTime, Input::isActionHeld(InputAction::MoveForward),
		    Input::isActionHeld(InputAction::MoveBackward),
		    Input::isActionHeld(InputAction::MoveLeft),
		    Input::isActionHeld(InputAction::MoveRight),
		    Input::getActionValue(InputAction::LookHorizontal),
		    Input::getActionValue(InputAction::LookVertical));

		Input::update();

		// Update transforms
		m_transformSystem.update(m_world);

		// Render
		m_renderSystem.drawFrame(m_swapChain, m_world, m_cameraSystem);
	}

	vkDeviceWaitIdle(m_context.getDevice());
}

void Application::cleanup() {
	m_renderSystem.cleanup();
	m_materialManager.cleanup();
	m_textureManager.cleanup();
	m_vulkanTextureBackend.cleanup();
	m_swapChain.cleanup();
	m_context.cleanup();

	glfwDestroyWindow(m_window);
	glfwTerminate();
}
