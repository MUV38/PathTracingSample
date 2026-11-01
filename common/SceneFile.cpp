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

uint64_t payloadHash(const std::vector<SceneVertex>& vertices)
{
	uint64_t hash = 14695981039346656037ull;
	const auto* bytes = reinterpret_cast<const unsigned char*>(vertices.data());
	for (size_t index = 0; index < vertices.size() * sizeof(SceneVertex); ++index)
	{
		hash = (hash ^ bytes[index]) * 1099511628211ull;
	}
	return hash;
}

void validate(const SceneData& scene)
{
	const auto& header = scene.mHeader;
	if (header.mMagic != SceneHeader{}.mMagic || header.mVersion != 1 || header.mVertexStride != sizeof(SceneVertex))
	{
		throw std::runtime_error("Unsupported scene binary format or version.");
	}
	if (header.mVertexCount == 0 || header.mVertexCount > maxVertices || header.mVertexCount % 3 != 0)
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
			if (!std::isfinite(vertex.mPosition[axis]) || !std::isfinite(vertex.mNormal[axis]) || !std::isfinite(vertex.mColor[axis]))
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
	const uint64_t payloadSize = scene.mHeader.mVertexCount * sizeof(SceneVertex);
	if (fileSize != sizeof(SceneHeader) + payloadSize)
	{
		throw std::runtime_error("Scene binary size does not match its header.");
	}
	scene.mVertices.resize(static_cast<size_t>(scene.mHeader.mVertexCount));
	if (!file.read(reinterpret_cast<char*>(scene.mVertices.data()), static_cast<std::streamsize>(payloadSize)))
	{
		throw std::runtime_error("Reading scene vertices failed.");
	}
	validate(scene);
	if (payloadHash(scene.mVertices) != scene.mHeader.mPayloadHash)
	{
		throw std::runtime_error("Scene binary checksum mismatch.");
	}
	return scene;
}

void writeSceneFile(const std::filesystem::path& path, SceneData& scene)
{
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
	scene.mHeader.mPayloadHash = payloadHash(scene.mVertices);
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
		file.write(reinterpret_cast<const char*>(scene.mVertices.data()), static_cast<std::streamsize>(scene.mVertices.size() * sizeof(SceneVertex)));
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
