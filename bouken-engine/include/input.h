#pragma once

#include "pch.h"

/**
 * TODO:
 * GamePad Backend
 * Config Files
 * Multi-Device Input
 * Buffering?
 */

class IInputBackend;

enum class InputAction {
	MoveForward,
	MoveBackward,
	MoveLeft,
	MoveRight,
	MoveUp,
	MoveDown,
	LookHorizontal,
	LookVertical,
	Jump,
	Crouch,
	Fire,
	ToggleMouseCapture,
};

enum class InputState {
	None,
	Pressed,
	Held,
	Released,
};

class Input {
   public:
	static void initialize(IInputBackend* backend);
	static void shutdown();
	static void update();

	static bool isActionPressed(InputAction action);
	static bool isActionHeld(InputAction action);
	static bool isActionReleased(InputAction action);
	static float getActionValue(
	    InputAction action);  // For analog inputs (-1 to 1)

	static void mapKeyToAction(int key, InputAction action);
	static void mapMouseButtonToAction(int button, InputAction action);

	static void setMouseCaptured(bool captured);
	static bool isMouseCaptured();

   private:
	static IInputBackend* s_backend;

	struct ActionState {
		InputState state = InputState::None;
		float value = 0.0f;  // For analog inputs
	};
	static std::unordered_map<InputAction, ActionState> s_actionStates;
	static std::unordered_map<InputAction, ActionState> s_previousActionStates;

	static std::unordered_map<int, InputAction> s_keyToAction;
	static std::unordered_map<int, InputAction> s_mouseButtonToAction;
};