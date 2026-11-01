#pragma once

#include <DirectXMath.h>

enum class CameraMode
{
	None,
	FreeLook,
	Orbit,
	Pan,
	Dolly
};

// Device-independent actions. The sample decides whether viewport or UI owns the input.
struct CameraInput
{
	CameraMode mMode = CameraMode::None;
	float mMouseDeltaX = 0.0f;
	float mMouseDeltaY = 0.0f;
	float mMoveForward = 0.0f;
	float mMoveRight = 0.0f;
	float mMoveUp = 0.0f;
	float mSpeedWheel = 0.0f;
	float mDollyWheel = 0.0f;
	float mViewportHeight = 720.0f;
	bool mFast = false;
};

class Camera final
{
public:
	Camera() { reset(); }
	void reset();
	void update(const CameraInput& input, float deltaTime);
	void setOrbit(float yaw, float pitch, float distance);
	void focus(const DirectX::XMFLOAT3& pivot);
	void setMoveSpeed(float speed);
	DirectX::XMFLOAT3 position() const { return mPosition; }
	DirectX::XMFLOAT3 pivot() const { return mPivot; }
	DirectX::XMFLOAT3 forward() const;
	DirectX::XMFLOAT3 right() const;
	DirectX::XMFLOAT3 up() const;
	float yaw() const { return mYaw; }
	float pitch() const { return mPitch; }
	float distance() const { return mDistance; }
	float moveSpeed() const { return mMoveSpeed; }
	float tanHalfFov() const;

private:
	void positionFromPivot();
	void pivotFromPosition();
	void translate(DirectX::FXMVECTOR offset);

	DirectX::XMFLOAT3 mPosition{};
	DirectX::XMFLOAT3 mPivot{};
	float mYaw = 0.65f;
	float mPitch = 0.45f;
	float mDistance = 6.0f;
	float mMoveSpeed = 3.0f;
};
