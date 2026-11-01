#include "Camera.h"

#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace
{
float distance(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b)
{
	return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}

void expect(bool condition, const char* description)
{
	if (!condition)
	{
		throw std::runtime_error(description);
	}
}

void testMovement()
{
	Camera camera;
	const auto initialPosition = camera.position();
	const auto initialPivot = camera.pivot();
	CameraInput input;
	input.mMode = CameraMode::FreeLook;
	input.mMoveForward = 1.0f;
	camera.update(input, 0.1f);
	expect(std::abs(distance(camera.position(), initialPosition) - 0.3f) < 0.0001f, "Forward speed must be 3 units/second.");
	expect(std::abs(distance(camera.pivot(), initialPivot) - 0.3f) < 0.0001f, "Free movement must translate the pivot too.");
	input.mMoveForward = -1.0f;
	camera.update(input, 0.1f);
	expect(distance(camera.position(), initialPosition) < 0.0001f, "Backward movement must undo forward movement.");
	Camera splitFrames;
	Camera oneFrame;
	input.mMoveForward = 1.0f;
	input.mMoveRight = 1.0f;
	oneFrame.update(input, 0.1f);
	splitFrames.update(input, 0.05f);
	splitFrames.update(input, 0.05f);
	expect(distance(oneFrame.position(), splitFrames.position()) < 0.0001f, "Movement must be frame-rate independent.");
	expect(std::abs(distance(oneFrame.position(), initialPosition) - 0.3f) < 0.0001f, "Diagonal movement must not be faster.");
	Camera fast;
	input.mMoveRight = 0.0f;
	input.mFast = true;
	fast.update(input, 0.1f);
	expect(std::abs(distance(fast.position(), initialPosition) - 1.2f) < 0.0001f, "Shift must provide a temporary 4x boost.");
	expect(fast.moveSpeed() == 3.0f, "Shift must not change the stored wheel speed.");
	Camera vertical;
	input = {};
	input.mMode = CameraMode::FreeLook;
	input.mMoveUp = 1.0f;
	vertical.update(input, 0.1f);
	expect(std::abs(vertical.position().y - initialPosition.y - 0.3f) < 0.0001f, "Vertical movement must use world up.");
}

void testWheelSpeed()
{
	Camera camera;
	const auto position = camera.position();
	CameraInput input;
	input.mSpeedWheel = 1.0f;
	camera.update(input, 0.016f);
	expect(camera.moveSpeed() > 3.0f, "Wheel up must increase movement speed.");
	expect(distance(camera.position(), position) == 0.0f, "Speed wheel must not dolly the camera.");
	input.mSpeedWheel = -1.0f;
	camera.update(input, 0.016f);
	expect(std::abs(camera.moveSpeed() - 3.0f) < 0.0001f, "Opposite wheel ticks must restore the original speed.");
	input.mSpeedWheel = 10000.0f;
	for (int index = 0; index < 10; ++index)
	{
		camera.update(input, 0.016f);
	}
	expect(camera.moveSpeed() == 100.0f, "Wheel speed must have an upper limit.");
	input.mSpeedWheel = -10000.0f;
	for (int index = 0; index < 10; ++index)
	{
		camera.update(input, 0.016f);
	}
	expect(camera.moveSpeed() == 0.05f, "Wheel speed must stay positive at the lower limit.");
}

void testLookOrbitPanAndDolly()
{
	Camera camera;
	const auto position = camera.position();
	CameraInput input;
	input.mMode = CameraMode::FreeLook;
	input.mMouseDeltaX = 100.0f;
	input.mMouseDeltaY = 30.0f;
	camera.update(input, 0.016f);
	expect(distance(camera.position(), position) == 0.0f, "Free look must rotate around the eye, not the origin.");
	expect(distance(camera.pivot(), {0, 0, 0}) > 0.1f, "Free look must move the future orbit pivot.");
	const DirectX::XMFLOAT3 pivot{3, 2, -4};
	camera.focus(pivot);
	input.mMode = CameraMode::Orbit;
	input.mMouseDeltaX = 200.0f;
	input.mMouseDeltaY = 100000.0f;
	camera.update(input, 0.016f);
	expect(distance(camera.pivot(), pivot) == 0.0f, "Orbit must preserve the chosen pivot.");
	expect(std::abs(distance(camera.position(), pivot) - 6.0f) < 0.0001f, "Orbit must preserve focus distance.");
	const auto forward = camera.forward();
	const auto right = camera.right();
	expect(std::isfinite(forward.x) && std::isfinite(forward.y) && std::isfinite(forward.z), "Extreme pitch must remain finite.");
	expect(std::abs(forward.x * right.x + forward.y * right.y + forward.z * right.z) < 0.0001f, "Camera basis must remain orthogonal near the poles.");
	const auto beforePan = camera.position();
	input.mMode = CameraMode::Pan;
	input.mMouseDeltaX = 60.0f;
	input.mMouseDeltaY = 40.0f;
	camera.update(input, 0.016f);
	const auto afterPan = camera.position();
	const auto afterPivot = camera.pivot();
	expect(distance(afterPan, beforePan) > 0.0f, "Pan must translate the camera.");
	expect(distance({afterPan.x - beforePan.x, afterPan.y - beforePan.y, afterPan.z - beforePan.z},
		{afterPivot.x - pivot.x, afterPivot.y - pivot.y, afterPivot.z - pivot.z}) < 0.0001f, "Pan must translate eye and pivot equally.");
	input = {};
	input.mDollyWheel = 1.0f;
	camera.update(input, 0.016f);
	expect(camera.distance() < 6.0f, "Alt+wheel up must move toward the pivot.");
	expect(distance(camera.pivot(), afterPivot) == 0.0f, "Dolly must preserve the pivot.");
	input.mDollyWheel = -1.0f;
	camera.update(input, 0.016f);
	expect(std::abs(camera.distance() - 6.0f) < 0.0001f, "Opposite dolly wheel ticks must restore focus distance.");
	input = {};
	input.mMode = CameraMode::Dolly;
	input.mMouseDeltaY = 30.0f;
	camera.update(input, 0.016f);
	expect(camera.distance() > 6.0f, "Alt+RMB drag must change focus distance.");
	camera.reset();
	expect(distance(camera.pivot(), {0, 0, 0}) == 0.0f && camera.moveSpeed() == 3.0f, "Reset must restore pivot and speed.");
}
}

int main()
{
	try
	{
		testMovement();
		testWheelSpeed();
		testLookOrbitPanAndDolly();
		std::puts("Camera tests passed: movement, frame timing, boost, speed wheel, look, orbit, pan, dolly, limits, reset.");
		return 0;
	}
	catch (const std::exception& error)
	{
		std::fprintf(stderr, "Camera test failed: %s\n", error.what());
		return 1;
	}
}
