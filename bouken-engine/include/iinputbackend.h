#pragma once

#include "pch.h"

// Abstract input backend interface
class IInputBackend {
   public:
	virtual ~IInputBackend() = default;

	// Polling
	virtual bool isKeyDown(int key) const = 0;
	virtual bool isMouseButtonDown(int button) const = 0;

	// Mouse state
	virtual float getMouseDeltaX() const = 0;
	virtual float getMouseDeltaY() const = 0;
	virtual void setMouseCaptured(bool captured) = 0;
	virtual bool isMouseCaptured() const = 0;

	// Frame update
	virtual void update() = 0;
};