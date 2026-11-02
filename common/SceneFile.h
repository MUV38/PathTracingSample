#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

// PTUSDM02: little-endian, v2 indexed meshlets only.
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
	std::array<char, 8> mMagic{'P', 'T', 'U', 'S', 'D', 'M', '0', '2'};
	uint32_t mVersion = 2;
	uint32_t mVertexStride = sizeof(SceneVertex);
	uint64_t mVertexCount = 0;
	uint64_t mMeshCount = 0;
	std::array<float, 3> mMinimum{};
	std::array<float, 3> mMaximum{};
	uint64_t mPayloadHash = 0;
};
static_assert(sizeof(SceneHeader) == 64);

struct SceneMeshletHeader
{
	uint64_t mMeshletCount = 0;
	uint64_t mVertexReferenceCount = 0;
	uint64_t mTriangleByteCount = 0;
};
static_assert(sizeof(SceneMeshletHeader) == 24);

// Offsets count uint32 vertex references and uint8 triangle indices, respectively.
struct SceneMeshlet
{
	uint32_t mVertexOffset;
	uint32_t mTriangleOffset;
	uint32_t mVertexCount;
	uint32_t mTriangleCount;
};
static_assert(sizeof(SceneMeshlet) == 16);

constexpr uint32_t sceneMeshletMaxVertices = 64;
constexpr uint32_t sceneMeshletMaxTriangles = 64;

struct SceneData
{
	SceneHeader mHeader;
	std::vector<SceneVertex> mVertices;
	std::vector<SceneMeshlet> mMeshlets;
	std::vector<uint32_t> mMeshletVertices;
	std::vector<uint8_t> mMeshletTriangles;
};

SceneData readSceneFile(const std::filesystem::path& path);
void writeSceneFile(const std::filesystem::path& path, SceneData& scene);
// Conventional indexed DXR input, in meshlet order. Local data remains available for CLAS.
std::vector<uint32_t> meshletTriangleIndices(const SceneData& scene);
