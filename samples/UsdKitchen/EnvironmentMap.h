#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

struct EnvironmentMap
{
	uint32_t mWidth = 0;
	uint32_t mHeight = 0;
	std::vector<std::array<float, 4>> mPixels;
};

EnvironmentMap createPreviewEnvironment();
EnvironmentMap readRadianceEnvironment(const std::filesystem::path& path);
