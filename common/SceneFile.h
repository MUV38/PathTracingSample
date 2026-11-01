#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

// PTUSDM01: little-endian, 64-byte header followed by packed triangle-list vertices.
// Positions are world-space meters in a right-handed, Y-up coordinate system.
struct SceneVertex
{
	std::array<float, 3> mPosition;
	std::array<float, 3> mNormal;
	std::array<float, 3> mColor;
};
static_assert(sizeof(SceneVertex) == 36);

struct SceneHeader
{
	std::array<char, 8> mMagic{'P', 'T', 'U', 'S', 'D', 'M', '0', '1'};
	uint32_t mVersion = 1;
	uint32_t mVertexStride = sizeof(SceneVertex);
	uint64_t mVertexCount = 0;
	uint64_t mMeshCount = 0;
	std::array<float, 3> mMinimum{};
	std::array<float, 3> mMaximum{};
	uint64_t mPayloadHash = 0;
};
static_assert(sizeof(SceneHeader) == 64);

struct SceneData
{
	SceneHeader mHeader;
	std::vector<SceneVertex> mVertices;
};

SceneData readSceneFile(const std::filesystem::path& path);
void writeSceneFile(const std::filesystem::path& path, SceneData& scene);
