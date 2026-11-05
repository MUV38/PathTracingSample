#pragma once

#include <cstdint>

struct PathTracing
{
	uint32_t mMaxBounces = 3;
	uint32_t mSamplesPerFrame = 1;
	float mEnvironmentIntensity = 1.0f;
	float mEnvironmentRotation = 0.0f;
	bool mEnvironmentMis = true;
	bool mEmissiveMis = true;
	bool mMultipleScattering = true;
	bool mRussianRoulette = true;
	uint32_t mRestirMode = 0;
	uint32_t mRestirDebug = 0;
	uint32_t mGiMode = 0;
	bool mGiHistory = true;
	bool mGiSpatial = true;
	uint32_t mGiView = 0;
	uint32_t mPathReplay = 0;
	bool mHybridUnoptimized = false;
	uint32_t mPtReservoirConfidence = 8;
	uint32_t mSeed = 0;
	uint32_t mPtRefreshPeriod = 0;
};
