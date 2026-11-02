#pragma once

#include <DirectXMath.h>

#include <array>

struct RectangleLighting
{
	bool mEnabled = false;
	bool mMis = true;
	DirectX::XMFLOAT3 mPosition{0.0f, 2.5f, 0.0f};
	float mWidth = 1.0f;
	float mHeight = 1.0f;
	float mAzimuth = 0.0f;
	float mElevation = -90.0f;
	std::array<float, 3> mColor{1.0f, 0.96f, 0.88f};
	float mRadiance = 10.0f;
};

struct Lighting
{
	RectangleLighting mRectangle;
	bool mPointLight = false;
	bool mShadows = true;
	bool mHeadlight = false;
	float mAzimuth = 35.0f;
	float mElevation = 55.0f;
	DirectX::XMFLOAT3 mPosition{1.2f, 2.5f, 1.0f};
	std::array<float, 3> mColor{1.0f, 0.96f, 0.88f};
	float mDirectionalStrength = 4.0f;
	float mPointStrength = 40.0f;
	float mAmbient = 0.0f;
	float mShadowBias = 0.0002f;
};
