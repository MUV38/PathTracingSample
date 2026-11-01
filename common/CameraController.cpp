#include "CameraController.h"
#include "Renderer.h"

#include <imgui.h>

void CameraController::update(Camera& camera, Renderer& renderer, float deltaTime, const DirectX::XMFLOAT3& focusPoint)
{
	const ImGuiIO& io = ImGui::GetIO();
	if (io.AppFocusLost)
	{
		mDrag = CameraMode::None;
		mRestoreCursor = false;
		return;
	}
	const ImGuiMouseButton dragButton = mDrag == CameraMode::Orbit ? ImGuiMouseButton_Left
		: mDrag == CameraMode::Pan ? ImGuiMouseButton_Middle : ImGuiMouseButton_Right;
	if (mDrag != CameraMode::None && !ImGui::IsMouseDown(dragButton))
	{
		mDrag = CameraMode::None;
		if (mRestoreCursor)
		{
			SetCursorPos(mDragCursorPosition.x, mDragCursorPosition.y);
			mRestoreCursor = false;
		}
	}
	bool startedDrag = false;
	if (mDrag == CameraMode::None && !io.WantCaptureMouse)
	{
		if (io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			mDrag = CameraMode::Orbit;
		}
		else if (io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
		{
			mDrag = CameraMode::Pan;
		}
		else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
		{
			mDrag = io.KeyAlt ? CameraMode::Dolly : CameraMode::FreeLook;
		}
		if (mDrag != CameraMode::None)
		{
			startedDrag = true;
			ImGui::SetWindowFocus(nullptr);
			mRestoreCursor = GetCursorPos(&mDragCursorPosition) != FALSE;
		}
	}
	CameraInput input;
	input.mMode = mDrag;
	input.mViewportHeight = static_cast<float>(renderer.height());
	if (mDrag != CameraMode::None)
	{
		ImGui::SetMouseCursor(ImGuiMouseCursor_None);
		if (!startedDrag)
		{
			const auto& motion = renderer.mouseMotion();
			input.mMouseDeltaX = motion.mUseRawMouse ? motion.mDeltaX : io.MouseDelta.x;
			input.mMouseDeltaY = motion.mUseRawMouse ? motion.mDeltaY : io.MouseDelta.y;
		}
	}
	if (mDrag == CameraMode::FreeLook && !io.WantTextInput && !ImGui::IsAnyItemActive())
	{
		input.mMoveForward = (ImGui::IsKeyDown(ImGuiKey_W) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_S) ? 1.0f : 0.0f);
		input.mMoveRight = (ImGui::IsKeyDown(ImGuiKey_D) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_A) ? 1.0f : 0.0f);
		input.mMoveUp = (ImGui::IsKeyDown(ImGuiKey_E) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_Q) ? 1.0f : 0.0f);
		input.mFast = io.KeyShift;
	}
	if (!io.WantCaptureMouse || mDrag != CameraMode::None)
	{
		input.mSpeedWheel = io.KeyAlt ? 0.0f : io.MouseWheel;
		input.mDollyWheel = io.KeyAlt ? io.MouseWheel : 0.0f;
	}
	if (!io.WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_F, false))
	{
		camera.focus(focusPoint);
	}
	camera.update(input, deltaTime);
}
