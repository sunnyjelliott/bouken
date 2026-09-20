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

// Everything a light entity needs, flat, so a scene reads as a list of values
// rather than a wall of createEntity/addComponent triples. Defaults mirror
// Light's own (lighting/light.h) - omit any field that doesn't apply to the
// type. Being a plain aggregate, a bulk set can also be written as a table:
//
//   static const LightDesc kColonnadeLights[] = { ... };
//   for (const LightDesc& desc : kColonnadeLights) spawnLight(m_world, desc);
struct LightDesc {
	LightType type = LightType::Point;
	glm::vec3 position = glm::vec3(0.0f);
	glm::vec3 eulerDegrees = glm::vec3(0.0f);  // pitch, yaw, roll
	glm::vec3 color = glm::vec3(1.0f);
	float intensity = 1.0f;
	float radius = 10.0f;      // Point + Spot
	float innerAngle = 15.0f;  // Spot, degrees
	float outerAngle = 30.0f;  // Spot, degrees
	bool castsShadow = false;
};

Entity spawnLight(World& world, const LightDesc& desc) {
	Entity entity = world.createEntity();

	Transform transform{};
	transform.position = desc.position;
	transform.rotation = glm::quat(glm::radians(desc.eulerDegrees));
	// TransformSystem::update doesn't run until mainLoop, but LightSystem and
	// ShadowSystem read worldMatrix - seed it now with the same call that
	// update would make. One formula covers all three types: a directional
	// light's direction comes from the rotation alone, so carrying its (zero)
	// translation costs nothing.
	transform.worldMatrix = transform.getLocalMatrix();
	world.addComponent(entity, transform);

	world.addComponent(entity, Light{.type = desc.type,
	                                 .color = desc.color,
	                                 .intensity = desc.intensity,
	                                 .radius = desc.radius,
	                                 .innerAngle = desc.innerAngle,
	                                 .outerAngle = desc.outerAngle,
	                                 .castsShadow = desc.castsShadow});

	return entity;
}
}  // namespace

Application::Application()
    : m_iblSystem(m_context),
      m_renderSystem(m_context, m_swapChain, m_lightSystem, m_shadowSystem,
                     m_iblSystem) {}

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
	    glfwCreateWindow(WIDTH, HEIGHT, "Bouken Engine", nullptr, nullptr);

	m_inputBackend = new GLFWInputBackend(m_window);

	Input::initialize(m_inputBackend);
	Input::setMouseCaptured(true);
}

void Application::initVulkan() {
	m_context.init(m_window);
	m_swapChain.init(m_context, m_window, WIDTH, HEIGHT);

	m_vulkanTextureBackend.initialize(m_context);
	m_textureManager.initialize(&m_vulkanTextureBackend);
	m_materialManager.initialize();

	m_lightSystem.initialize(m_context);
	m_shadowSystem.initialize(m_context);
	m_iblSystem.init();
	m_renderSystem.initialize();
	m_renderSystem.updateIBLDescriptors();
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
	    Camera{.fov = 45.0f, .nearPlane = 0.1f, .farPlane = 1000.0f});
	m_cameraSystem.setActiveCamera(m_activeCamera);

	// Directional sun - warm, from above and to one side
	spawnLight(m_world, {.type = LightType::Directional,
	                     .eulerDegrees = {-60.0f, 45.0f, 0.0f},
	                     .color = {1.0f, 0.95f, 0.8f},
	                     .intensity = 3.0f,
	                     .castsShadow = true});

	for (int i = 0; i < 3; ++i) {
		// Warm point light
		spawnLight(m_world, {.type = LightType::Point,
		                     .position = {-4.0f + (4.0f * i), 7.0f, 1.0f},
		                     .color = {1.0f, 0.85f, 0.6f},
		                     .intensity = 200.0f,
		                     .radius = 10.0f,
		                     .castsShadow = true});

		// Cool point light
		spawnLight(m_world, {.type = LightType::Point,
		                     .position = {4.0f - (4.0f * i), 7.0f, -1.0f},
		                     .color = {0.6f, 0.8f, 1.0f},
		                     .intensity = 150.0f,
		                     .radius = 10.0f,
		                     .castsShadow = true});
	}

	// Spot light pointed at two pillars
	spawnLight(m_world, {.type = LightType::Spot,
	                     .position = {-2.0f, 3.0f, 4.0f},
	                     .eulerDegrees = {-30.0f, 90.0f, 0.0f},
	                     .color = {1.0f, 1.0f, 0.9f},
	                     .intensity = 500.0f,
	                     .radius = 10.0f,
	                     .innerAngle = 15.0f,
	                     .outerAngle = 30.0f,
	                     .castsShadow = true});

	// Second Spot light pointed at two pillars
	spawnLight(m_world, {.type = LightType::Spot,
	                     .position = {-4.0f, 3.0f, 0.0f},
	                     .eulerDegrees = {-30.0f, 180.0f, 0.0f},
	                     .color = {1.0f, 1.0f, 0.9f},
	                     .intensity = 500.0f,
	                     .radius = 10.0f,
	                     .innerAngle = 15.0f,
	                     .outerAngle = 30.0f,
	                     .castsShadow = true});

	// Additional scene lights go here.

	SceneLoadOptions options;
	options.createHeirarchy = true;
	std::filesystem::path scenePath = std::filesystem::current_path() /
	                                  "assets" / "models" / "main_sponza" /
	                                  "sponza.usdc";

	SceneLoader::loadScene(scenePath.string(), m_world, m_renderSystem,
	                       m_textureManager, m_materialManager, options);

	std::filesystem::path hdrPath = std::filesystem::current_path() / "assets" /
	                                "models" / "main_sponza" / "textures" /
	                                "kloppenheim_05_4k.hdr";

	if (std::filesystem::exists(hdrPath)) {
		m_iblSystem.loadEnvironment(hdrPath.string());
		m_renderSystem.updateIBLDescriptors();
	} else {
		std::cout << "  IBL environment not found at expected path - rendering "
		             "without image-based lighting"
		          << std::endl;
	}

	m_renderSystem.createMaterialDescriptorSets(m_materialManager,
	                                            m_textureManager);
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
		    Input::getActionValue(InputAction::LookVertical),
		    Input::isActionHeld(InputAction::Sprint));

		Input::update();

		// Update transforms, then derive world-space bounds from them.
		// Ordering is load-bearing: drawFrame runs ShadowSystem::collectCasters
		// and gatherRenderItems, both of which cull against BoundingBox::world.
		m_transformSystem.update(m_world);
		m_boundsSystem.update(m_world);

		// Render
		m_renderSystem.drawFrame(m_swapChain, m_world, m_cameraSystem,
		                         m_materialManager);
	}

	vkDeviceWaitIdle(m_context.getDevice());
}

void Application::cleanup() {
	m_renderSystem.cleanup();
	m_shadowSystem.cleanup();
	m_lightSystem.cleanup();
	m_iblSystem.cleanup();
	m_materialManager.cleanup();
	m_textureManager.cleanup();
	m_vulkanTextureBackend.cleanup();
	m_swapChain.cleanup();
	m_context.cleanup();

	glfwDestroyWindow(m_window);
	glfwTerminate();
}
