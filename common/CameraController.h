#pragma once

#include "Camera.h"

#include <windows.h>

class Renderer;

// Win32 / ImGui adapter for the reusable, device-independent camera.
class CameraController final
{
public:
	void update(Camera& camera, Renderer& renderer, float deltaTime, const DirectX::XMFLOAT3& focusPoint = {});

private:
	CameraMode mDrag = CameraMode::None;
	POINT mDragCursorPosition{};
	bool mRestoreCursor = false;
};
