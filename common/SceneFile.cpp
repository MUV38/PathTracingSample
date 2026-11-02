#include "SceneFile.h"

#include <windows.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace
{
static_assert(std::endian::native == std::endian::little);
static_assert(std::numeric_limits<float>::is_iec559);
constexpr uint64_t maxVertices = 30000000;

template<typename T> void hashArray(uint64_t& hash, const std::vector<T>& values)
{
	const auto* bytes = reinterpret_cast<const unsigned char*>(values.data());
	for (size_t index = 0; index < values.size() * sizeof(T); ++index)
	{
		hash = (hash ^ bytes[index]) * 1099511628211ull;
	}
}

uint64_t payloadHash(const SceneData& scene)
{
	uint64_t hash = 14695981039346656037ull;
	hashArray(hash, scene.mVertices);
	hashArray(hash, scene.mMeshlets);
	hashArray(hash, scene.mMeshletVertices);
	hashArray(hash, scene.mMeshletTriangles);
	return hash;
}

void validate(const SceneData& scene)
{
	const auto& header = scene.mHeader;
	if (header.mMagic != SceneHeader{}.mMagic || header.mVersion != 2 || header.mVertexStride != sizeof(SceneVertex))
	{
		throw std::runtime_error("Unsupported scene binary format or version.");
	}
	if (header.mVertexCount == 0 || header.mVertexCount > maxVertices)
	{
		throw std::runtime_error("Invalid triangle-list vertex count in scene binary.");
	}
	for (size_t axis = 0; axis < 3; ++axis)
	{
		if (!std::isfinite(header.mMinimum[axis]) || !std::isfinite(header.mMaximum[axis]) || header.mMinimum[axis] > header.mMaximum[axis])
		{
			throw std::runtime_error("Invalid scene bounds.");
		}
	}
	for (const auto& vertex : scene.mVertices)
	{
		for (size_t axis = 0; axis < 3; ++axis)
		{
			if (!std::isfinite(vertex.mPosition[axis]) || !std::isfinite(vertex.mNormal[axis]) || !std::isfinite(vertex.mColor[axis])
				|| vertex.mPosition[axis] < header.mMinimum[axis] || vertex.mPosition[axis] > header.mMaximum[axis])
			{
				throw std::runtime_error("Scene binary contains non-finite vertex data.");
			}
		}
		const auto& n = vertex.mNormal;
		if (n[0] * n[0] + n[1] * n[1] + n[2] * n[2] < 0.000001f)
		{
			throw std::runtime_error("Scene binary contains a zero-length normal.");
		}
	}
}

void validateMeshlets(const SceneData& scene)
{
	if (scene.mVertices.size() != scene.mHeader.mVertexCount || scene.mMeshlets.empty() || scene.mMeshlets.size() > maxVertices / 3
		|| scene.mMeshletVertices.size() > maxVertices || scene.mMeshletTriangles.size() > maxVertices)
	{
		throw std::runtime_error("Invalid meshlet array sizes.");
	}
	size_t vertexOffset = 0, triangleOffset = 0;
	for (const auto& meshlet : scene.mMeshlets)
	{
		if (meshlet.mVertexCount == 0 || meshlet.mVertexCount > sceneMeshletMaxVertices
			|| meshlet.mTriangleCount == 0 || meshlet.mTriangleCount > sceneMeshletMaxTriangles
			|| meshlet.mVertexOffset != vertexOffset || meshlet.mTriangleOffset != triangleOffset
			|| meshlet.mVertexCount > scene.mMeshletVertices.size() - vertexOffset
			|| meshlet.mTriangleCount * 3 > scene.mMeshletTriangles.size() - triangleOffset)
		{
			throw std::runtime_error("Invalid meshlet range or limits.");
		}
		for (size_t index = 0; index < meshlet.mTriangleCount * 3; ++index)
		{
			if (scene.mMeshletTriangles[triangleOffset + index] >= meshlet.mVertexCount)
			{
				throw std::runtime_error("Meshlet local triangle index is out of range.");
			}
		}
		vertexOffset += meshlet.mVertexCount;
		triangleOffset += meshlet.mTriangleCount * 3;
	}
	if (vertexOffset != scene.mMeshletVertices.size() || triangleOffset != scene.mMeshletTriangles.size())
	{
		throw std::runtime_error("Meshlet ranges do not cover the payload.");
	}
	for (uint32_t index : scene.mMeshletVertices)
	{
		if (index >= scene.mVertices.size())
		{
			throw std::runtime_error("Meshlet global vertex index is out of range.");
		}
	}
}

template<typename T> void readArray(std::ifstream& file, std::vector<T>& values, uint64_t count)
{
	values.resize(static_cast<size_t>(count));
	if (!file.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(count * sizeof(T))))
	{
		throw std::runtime_error("Reading scene payload failed.");
	}
}

template<typename T> void writeArray(std::ofstream& file, const std::vector<T>& values)
{
	file.write(reinterpret_cast<const char*>(values.data()), static_cast<std::streamsize>(values.size() * sizeof(T)));
}

}

SceneData readSceneFile(const std::filesystem::path& path)
{
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file || file.tellg() < static_cast<std::streamoff>(sizeof(SceneHeader)))
	{
		throw std::runtime_error("Cannot open scene binary, or its header is truncated.");
	}
	const auto fileSize = static_cast<uint64_t>(file.tellg());
	file.seekg(0);
	SceneData scene;
	if (!file.read(reinterpret_cast<char*>(&scene.mHeader), sizeof(SceneHeader)))
	{
		throw std::runtime_error("Reading the scene header failed.");
	}
	validate(scene); // Validate counts before allocating memory.
	uint64_t expectedSize = sizeof(SceneHeader) + scene.mHeader.mVertexCount * sizeof(SceneVertex);
	SceneMeshletHeader meshlets;
	if (!file.read(reinterpret_cast<char*>(&meshlets), sizeof(meshlets))
		|| meshlets.mMeshletCount == 0 || meshlets.mMeshletCount > maxVertices / 3
		|| meshlets.mVertexReferenceCount == 0 || meshlets.mVertexReferenceCount > maxVertices
		|| meshlets.mTriangleByteCount == 0 || meshlets.mTriangleByteCount > maxVertices || meshlets.mTriangleByteCount % 3 != 0)
	{
		throw std::runtime_error("Invalid meshlet header.");
	}
	expectedSize += sizeof(meshlets) + meshlets.mMeshletCount * sizeof(SceneMeshlet)
		+ meshlets.mVertexReferenceCount * sizeof(uint32_t) + meshlets.mTriangleByteCount;
	if (fileSize != expectedSize)
	{
		throw std::runtime_error("Scene binary size does not match its header.");
	}
	readArray(file, scene.mVertices, scene.mHeader.mVertexCount);
	readArray(file, scene.mMeshlets, meshlets.mMeshletCount);
	readArray(file, scene.mMeshletVertices, meshlets.mVertexReferenceCount);
	readArray(file, scene.mMeshletTriangles, meshlets.mTriangleByteCount);
	validate(scene);
	if (payloadHash(scene) != scene.mHeader.mPayloadHash)
	{
		throw std::runtime_error("Scene binary checksum mismatch.");
	}
	validateMeshlets(scene);
	return scene;
}

void writeSceneFile(const std::filesystem::path& path, SceneData& scene)
{
	scene.mHeader.mVersion = 2;
	scene.mHeader.mVertexCount = scene.mVertices.size();
	scene.mHeader.mMinimum.fill((std::numeric_limits<float>::max)());
	scene.mHeader.mMaximum.fill((std::numeric_limits<float>::lowest)());
	for (const auto& vertex : scene.mVertices)
	{
		for (size_t axis = 0; axis < 3; ++axis)
		{
			scene.mHeader.mMinimum[axis] = (std::min)(scene.mHeader.mMinimum[axis], vertex.mPosition[axis]);
			scene.mHeader.mMaximum[axis] = (std::max)(scene.mHeader.mMaximum[axis], vertex.mPosition[axis]);
		}
	}
	validate(scene);
	validateMeshlets(scene);
	scene.mHeader.mPayloadHash = payloadHash(scene);
	const SceneMeshletHeader meshlets{scene.mMeshlets.size(), scene.mMeshletVertices.size(), scene.mMeshletTriangles.size()};
	if (!path.parent_path().empty())
	{
		std::filesystem::create_directories(path.parent_path());
	}
	auto temporary = path;
	temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId());
	try
	{
		std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
		file.write(reinterpret_cast<const char*>(&scene.mHeader), sizeof(SceneHeader));
		file.write(reinterpret_cast<const char*>(&meshlets), sizeof(meshlets));
		writeArray(file, scene.mVertices);
		writeArray(file, scene.mMeshlets);
		writeArray(file, scene.mMeshletVertices);
		writeArray(file, scene.mMeshletTriangles);
		file.close();
		if (!file || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		{
			throw std::runtime_error("Writing the scene binary failed.");
		}
	}
	catch (...)
	{
		std::error_code error;
		std::filesystem::remove(temporary, error);
		throw;
	}
}

std::vector<uint32_t> meshletTriangleIndices(const SceneData& scene)
{
	validate(scene);
	validateMeshlets(scene);
	std::vector<uint32_t> indices;
	indices.reserve(scene.mMeshletTriangles.size());
	for (const auto& meshlet : scene.mMeshlets)
	{
		for (uint32_t index = 0; index < meshlet.mTriangleCount * 3; ++index)
		{
			indices.push_back(scene.mMeshletVertices[meshlet.mVertexOffset + scene.mMeshletTriangles[meshlet.mTriangleOffset + index]]);
		}
	}
	return indices;
}
