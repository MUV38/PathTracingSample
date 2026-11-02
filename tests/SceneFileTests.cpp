#include "SceneFile.h"
#include "MeshletBuilder.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <stdexcept>

namespace
{
void require(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

void expectRejected(const std::filesystem::path& path)
{
	try
	{
		readSceneFile(path);
	}
	catch (const std::runtime_error&)
	{
		return;
	}
	throw std::runtime_error("Malformed scene was accepted.");
}

void writeBytes(const std::filesystem::path& path, const std::vector<char>& bytes)
{
	std::ofstream stream(path, std::ios::binary | std::ios::trunc);
	stream.write(bytes.data(), bytes.size());
	require(static_cast<bool>(stream), "Cannot write test data.");
}

void checkConvertedFixture(const std::filesystem::path& path)
{
	const auto scene = readSceneFile(path);
	const auto indices = meshletTriangleIndices(scene);
	require(scene.mHeader.mMeshCount == 1 && indices.size() == 9, "Concave polygon should produce three triangles.");
	const std::array<float, 3> minimum{1, 3, -4}, maximum{3, 3, -2};
	for (size_t axis = 0; axis < 3; ++axis)
	{
		require(std::abs(scene.mHeader.mMinimum[axis] - minimum[axis]) < 0.0001f, "USD minimum: transform, units or up axis incorrect.");
		require(std::abs(scene.mHeader.mMaximum[axis] - maximum[axis]) < 0.0001f, "USD maximum: transform, units or up axis incorrect.");
	}
	float area = 0;
	for (size_t index = 0; index < indices.size(); index += 3)
	{
		const auto& a = scene.mVertices[indices[index]].mPosition;
		const auto& b = scene.mVertices[indices[index + 1]].mPosition;
		const auto& c = scene.mVertices[indices[index + 2]].mPosition;
		area += std::abs((b[0] - a[0]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[0] - a[0])) * 0.5f;
	}
	require(std::abs(area - 3.0f) < 0.0001f, "Concave polygon triangulation changed its area.");
	for (const auto& vertex : scene.mVertices)
	{
		require(vertex.mNormal[1] > 0.999f, "Authored normal was not transformed to Y-up.");
		require(std::abs(vertex.mColor[0] - 0.2f) < 0.0001f, "Indexed displayColor was lost.");
	}
}

auto triangleSet(const std::vector<SceneVertex>& vertices, const std::vector<uint32_t>& indices)
{
	std::map<std::array<float, 27>, size_t> triangles;
	for (size_t first = 0; first < indices.size(); first += 3)
	{
		std::array<float, 27> key{};
		for (size_t corner = 0; corner < 3; ++corner)
		{
			const auto& vertex = vertices[indices[first + corner]];
			std::copy(vertex.mPosition.begin(), vertex.mPosition.end(), key.begin() + corner * 9);
			std::copy(vertex.mNormal.begin(), vertex.mNormal.end(), key.begin() + corner * 9 + 3);
			std::copy(vertex.mColor.begin(), vertex.mColor.end(), key.begin() + corner * 9 + 6);
		}
		auto best = key;
		for (size_t rotation = 0; rotation < 2; ++rotation)
		{
			std::rotate(key.begin(), key.begin() + 9, key.end());
			best = (std::min)(best, key);
		}
		++triangles[best];
	}
	return triangles;
}

void checkMeshletBuilder(const std::filesystem::path& directory)
{
	SceneData grid;
	for (int y = 0; y < 12; ++y)
	{
		for (int x = 0; x < 12; ++x)
		{
			const auto vertex = [](int x, int y)
			{
				return SceneVertex{{static_cast<float>(x), static_cast<float>(y), 0}, {0, 0, 1}, {0.2f, 0.4f, 0.6f}};
			};
			for (auto v : {vertex(x,y), vertex(x+1,y), vertex(x+1,y+1), vertex(x,y), vertex(x+1,y+1), vertex(x,y+1)})
			{
				grid.mVertices.push_back(v);
			}
		}
	}
	// Coincident geometry with different attributes must remain distinct.
	for (size_t index = 0; index < 3; ++index)
	{
		auto vertex = grid.mVertices[index];
		vertex.mColor = {1, 0, 0};
		vertex.mNormal = {0, 0, -1};
		grid.mVertices.push_back(vertex);
	}
	std::vector<uint32_t> sequential(grid.mVertices.size());
	std::iota(sequential.begin(), sequential.end(), 0u);
	const auto expected = triangleSet(grid.mVertices, sequential);
	buildSceneMeshlets(grid);
	require(grid.mMeshlets.size() > 1 && grid.mVertices.size() < sequential.size(), "Meshlet subdivision or vertex deduplication failed.");
	const auto path = directory / L"grid.ptscene";
	writeSceneFile(path, grid);
	const auto loaded = readSceneFile(path);
	require(expected == triangleSet(loaded.mVertices, meshletTriangleIndices(loaded)), "Meshlets lost triangles, winding, normals or colors.");
	for (int mutation = 0; mutation < 4; ++mutation)
	{
		auto invalid = loaded;
		if (mutation == 0)
		{
			invalid.mMeshletTriangles[0] = 255;
		}
		else if (mutation == 1)
		{
			invalid.mMeshletVertices[0] = UINT32_MAX;
		}
		else if (mutation == 2)
		{
			invalid.mMeshlets[0].mTriangleCount = 65;
		}
		else
		{
			invalid.mMeshlets[1].mVertexOffset = UINT32_MAX;
		}
		bool rejected = false;
		try
		{
			writeSceneFile(path, invalid);
		}
		catch (const std::runtime_error&)
		{
			rejected = true;
		}
		require(rejected, "Invalid meshlet topology was accepted.");
	}
	// Unsupported versions must be rejected before allocating any payload.
	auto header = loaded.mHeader;
	header.mVersion = 1;
	std::ofstream unsupported(directory / L"unsupported.ptscene", std::ios::binary);
	unsupported.write(reinterpret_cast<const char*>(&header), sizeof(header));
	unsupported.close();
	expectRejected(directory / L"unsupported.ptscene");
}
}

int wmain(int count, wchar_t** arguments)
{
	std::filesystem::path directory;
	try
	{
		require(count >= 2 && count <= 3, "Usage: SceneFileTests <test-output-directory> [converted-ConversionFixture.ptscene]");
		directory = std::filesystem::path(arguments[1]) / (L"scene-tests-" + std::to_wstring(GetCurrentProcessId()));
		require(std::filesystem::create_directories(directory), "Test directory already exists.");
		const auto path = directory / L"roundtrip.ptscene";
		SceneData source;
		source.mHeader.mMeshCount = 1;
		source.mVertices = {{{-2, 1, 0}, {0, 0, 1}, {1, 0, 0}}, {{4, 1, 0}, {0, 0, 1}, {0, 1, 0}}, {{0, 3, 0}, {0, 0, 1}, {0, 0, 1}}};
		buildSceneMeshlets(source);
		writeSceneFile(path, source);
		const auto loaded = readSceneFile(path);
		require(loaded.mHeader.mMinimum == std::array<float, 3>{-2, 1, 0}, "Minimum bounds incorrect.");
		require(loaded.mHeader.mMaximum == std::array<float, 3>{4, 3, 0}, "Maximum bounds incorrect.");
		require(loaded.mVertices.size() == 3 && std::memcmp(loaded.mVertices.data(), source.mVertices.data(), 3 * sizeof(SceneVertex)) == 0, "Round trip changed vertices.");
		std::ifstream stream(path, std::ios::binary);
		const std::vector<char> original{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
		stream.close();
		const auto malformed = directory / L"malformed.ptscene";
		for (size_t length : {size_t(0), size_t(63), original.size() - 1})
		{
			writeBytes(malformed, {original.begin(), original.begin() + length});
			expectRejected(malformed);
		}
		for (size_t offset : {size_t(0), size_t(8), size_t(12), size_t(64)})
		{
			auto bytes = original;
			bytes[offset] ^= 1;
			writeBytes(malformed, bytes);
			expectRejected(malformed);
		}
		auto bytes = original;
		SceneHeader header = loaded.mHeader;
		header.mVertexCount = UINT64_MAX;
		std::memcpy(bytes.data(), &header, sizeof(header));
		writeBytes(malformed, bytes);
		expectRejected(malformed);
		bytes = original;
		bytes.push_back(0);
		writeBytes(malformed, bytes);
		expectRejected(malformed);
		// A failed export must preserve the existing valid file.
		source.mVertices[0].mPosition[0] = std::numeric_limits<float>::quiet_NaN();
		bool rejected = false;
		try
		{
			writeSceneFile(path, source);
		}
		catch (const std::runtime_error&)
		{
			rejected = true;
		}
		require(rejected, "Non-finite export was accepted.");
		require(readSceneFile(path).mVertices.size() == 3, "Failed export destroyed the previous file.");
		if (count == 3)
		{
			checkConvertedFixture(arguments[2]);
		}
		checkMeshletBuilder(directory);
		std::cout << "Scene file tests passed.\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
