#include "Camera.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

void Camera::reset()
{
	mPivot = {};
	mMoveSpeed = 3.0f;
	setOrbit(0.65f, 0.45f, 6.0f);
}

void Camera::setOrbit(float yaw, float pitch, float distance)
{
	mYaw = std::remainder(yaw, XM_2PI);
	mPitch = std::clamp(pitch, -XM_PIDIV2 + 0.01f, XM_PIDIV2 - 0.01f);
	mDistance = std::clamp(distance, 0.1f, 1000.0f);
	positionFromPivot();
}

void Camera::setMoveSpeed(float speed)
{
	mMoveSpeed = std::clamp(speed, 0.05f, 100.0f);
}

XMFLOAT3 Camera::forward() const
{
	return {-std::sin(mYaw) * std::cos(mPitch), -std::sin(mPitch), -std::cos(mYaw) * std::cos(mPitch)};
}

XMFLOAT3 Camera::right() const
{
	return {std::cos(mYaw), 0.0f, -std::sin(mYaw)};
}

XMFLOAT3 Camera::up() const
{
	const auto f = forward();
	const auto r = right();
	XMFLOAT3 result;
	XMStoreFloat3(&result, XMVector3Cross(XMLoadFloat3(&r), XMLoadFloat3(&f)));
	return result;
}

float Camera::tanHalfFov() const
{
	return std::tan(XM_PIDIV4 * 0.5f);
}

void Camera::positionFromPivot()
{
	const auto f = forward();
	XMStoreFloat3(&mPosition, XMLoadFloat3(&mPivot) - XMLoadFloat3(&f) * mDistance);
}

void Camera::pivotFromPosition()
{
	const auto f = forward();
	XMStoreFloat3(&mPivot, XMLoadFloat3(&mPosition) + XMLoadFloat3(&f) * mDistance);
}

void Camera::translate(FXMVECTOR offset)
{
	XMStoreFloat3(&mPosition, XMLoadFloat3(&mPosition) + offset);
	XMStoreFloat3(&mPivot, XMLoadFloat3(&mPivot) + offset);
}

void Camera::focus(const XMFLOAT3& pivot)
{
	mPivot = pivot;
	positionFromPivot();
}

void Camera::update(const CameraInput& input, float deltaTime)
{
	setMoveSpeed(mMoveSpeed * std::exp(std::clamp(input.mSpeedWheel, -20.0f, 20.0f) * 0.2f));
	if (input.mMode == CameraMode::FreeLook || input.mMode == CameraMode::Orbit)
	{
		mYaw = std::remainder(mYaw - input.mMouseDeltaX * 0.004f, XM_2PI);
		mPitch = std::clamp(mPitch + input.mMouseDeltaY * 0.004f, -XM_PIDIV2 + 0.01f, XM_PIDIV2 - 0.01f);
		if (input.mMode == CameraMode::FreeLook)
		{
			pivotFromPosition();
		}
		else
		{
			positionFromPivot();
		}
	}
	if (input.mMode == CameraMode::Pan)
	{
		const auto r = right();
		const auto u = up();
		const float scale = 2.0f * mDistance * tanHalfFov() / (std::max)(input.mViewportHeight, 1.0f);
		translate((-XMLoadFloat3(&r) * input.mMouseDeltaX + XMLoadFloat3(&u) * input.mMouseDeltaY) * scale);
	}
	const float dolly = (input.mMode == CameraMode::Dolly ? input.mMouseDeltaY * 0.01f : 0.0f) - input.mDollyWheel * 0.15f;
	if (dolly != 0.0f)
	{
		mDistance = std::clamp(mDistance * std::exp(std::clamp(dolly, -4.0f, 4.0f)), 0.1f, 1000.0f);
		positionFromPivot();
	}
	if (input.mMode == CameraMode::FreeLook)
	{
		const auto f = forward();
		const auto r = right();
		XMVECTOR direction = XMLoadFloat3(&f) * input.mMoveForward + XMLoadFloat3(&r) * input.mMoveRight
			+ XMVectorSet(0, input.mMoveUp, 0, 0);
		if (XMVectorGetX(XMVector3LengthSq(direction)) > 1.0f)
		{
			direction = XMVector3Normalize(direction);
		}
		const float speed = mMoveSpeed * (input.mFast ? 4.0f : 1.0f);
		translate(direction * speed * std::clamp(deltaTime, 0.0f, 0.1f));
	}
}
