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
};
