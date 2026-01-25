#include "input.h"

#include "iinputbackend.h"

IInputBackend* Input::s_backend = nullptr;
std::unordered_map<InputAction, Input::ActionState> Input::s_actionStates;
std::unordered_map<InputAction, Input::ActionState>
    Input::s_previousActionStates;
std::unordered_map<int, InputAction> Input::s_keyToAction;
std::unordered_map<int, InputAction> Input::s_mouseButtonToAction;

void Input::initialize(IInputBackend* backend) {
	s_backend = backend;

	// Set up default key mappings (can be changed at runtime)
	// Using GLFW key codes for now - ideally these would be backend-agnostic
	// codes
	mapKeyToAction(87, InputAction::MoveForward);          // W
	mapKeyToAction(83, InputAction::MoveBackward);         // S
	mapKeyToAction(65, InputAction::MoveLeft);             // A
	mapKeyToAction(68, InputAction::MoveRight);            // D
	mapKeyToAction(32, InputAction::MoveUp);               // Space
	mapKeyToAction(340, InputAction::MoveDown);            // Left Shift
	mapKeyToAction(256, InputAction::ToggleMouseCapture);  // Escape
}

void Input::shutdown() {
	s_backend = nullptr;
	s_actionStates.clear();
	s_previousActionStates.clear();
	s_keyToAction.clear();
	s_mouseButtonToAction.clear();
}

void Input::update() {
	if (!s_backend) return;

	// Save previous states
	s_previousActionStates = s_actionStates;

	// Update all action states based on key mappings
	for (const auto& [key, action] : s_keyToAction) {
		bool isDown = s_backend->isKeyDown(key);
		bool wasDown =
		    s_previousActionStates[action].state == InputState::Pressed ||
		    s_previousActionStates[action].state == InputState::Held;

		if (isDown && !wasDown) {
			s_actionStates[action].state = InputState::Pressed;
			s_actionStates[action].value = 1.0f;
		} else if (isDown && wasDown) {
			s_actionStates[action].state = InputState::Held;
			s_actionStates[action].value = 1.0f;
		} else if (!isDown && wasDown) {
			s_actionStates[action].state = InputState::Released;
			s_actionStates[action].value = 0.0f;
		} else {
			s_actionStates[action].state = InputState::None;
			s_actionStates[action].value = 0.0f;
		}
	}

	// Update analog inputs (mouse)
	s_actionStates[InputAction::LookHorizontal].value =
	    s_backend->getMouseDeltaX();
	s_actionStates[InputAction::LookVertical].value =
	    s_backend->getMouseDeltaY();

	// Update backend (clears deltas, etc.)
	s_backend->update();
}

bool Input::isActionPressed(InputAction action) {
	auto it = s_actionStates.find(action);
	return it != s_actionStates.end() &&
	       it->second.state == InputState::Pressed;
}

bool Input::isActionHeld(InputAction action) {
	auto it = s_actionStates.find(action);
	return it != s_actionStates.end() &&
	       (it->second.state == InputState::Pressed ||
	        it->second.state == InputState::Held);
}

bool Input::isActionReleased(InputAction action) {
	auto it = s_actionStates.find(action);
	return it != s_actionStates.end() &&
	       it->second.state == InputState::Released;
}

float Input::getActionValue(InputAction action) {
	auto it = s_actionStates.find(action);
	return it != s_actionStates.end() ? it->second.value : 0.0f;
}

void Input::mapKeyToAction(int key, InputAction action) {
	s_keyToAction[key] = action;
}

void Input::mapMouseButtonToAction(int button, InputAction action) {
	s_mouseButtonToAction[button] = action;
}

void Input::setMouseCaptured(bool captured) {
	if (s_backend) {
		s_backend->setMouseCaptured(captured);
	}
}

bool Input::isMouseCaptured() {
	return s_backend ? s_backend->isMouseCaptured() : false;
}
