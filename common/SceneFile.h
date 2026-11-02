#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

// PTUSDM03: little-endian, indexed meshlets with a PBR material table.
// Positions are world-space meters in a right-handed, Y-up coordinate system.
struct SceneVertex
{
	std::array<float, 3> mPosition;
	std::array<float, 3> mNormal;
	std::array<float, 3> mColor;
	uint32_t mMaterialIndex = 0;
};
static_assert(sizeof(SceneVertex) == 40);

struct SceneMaterial
{
	std::array<float, 3> mBaseColor{1, 1, 1};
	float mMetallic = 0.0f;
	std::array<float, 3> mEmissiveColor{};
	float mRoughness = 0.5f;
	float mSpecular = 0.5f; // Dielectric F0 = 0.08 * Specular (0.5 -> 4%).
	float mOcclusion = 1.0f;
	float mEmissiveStrength = 1.0f;
	float mPadding = 0.0f;
};
static_assert(sizeof(SceneMaterial) == 48);

struct SceneHeader
{
	std::array<char, 8> mMagic{'P', 'T', 'U', 'S', 'D', 'M', '0', '3'};
	uint32_t mVersion = 3;
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
	uint64_t mMaterialCount = 0;
};
static_assert(sizeof(SceneMeshletHeader) == 32);

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
constexpr uint32_t sceneMeshletMaxTriangles = 126;

struct SceneData
{
	SceneHeader mHeader;
	std::vector<SceneVertex> mVertices;
	std::vector<SceneMaterial> mMaterials{SceneMaterial{}};
	std::vector<SceneMeshlet> mMeshlets;
	std::vector<uint32_t> mMeshletVertices;
	std::vector<uint8_t> mMeshletTriangles;
};

SceneData readSceneFile(const std::filesystem::path& path);
void writeSceneFile(const std::filesystem::path& path, SceneData& scene);
// Conventional indexed DXR input, in meshlet order. Local data remains available for CLAS.
std::vector<uint32_t> meshletTriangleIndices(const SceneData& scene);
void validateSceneMaterials(const std::vector<SceneMaterial>& materials);
