#include "SceneFile.h"

#include <windows.h>

#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
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
	require(scene.mHeader.mMeshCount == 1 && scene.mVertices.size() == 9, "Concave polygon should produce three triangles.");
	const std::array<float, 3> minimum{1, 3, -4}, maximum{3, 3, -2};
	for (size_t axis = 0; axis < 3; ++axis)
	{
		require(std::abs(scene.mHeader.mMinimum[axis] - minimum[axis]) < 0.0001f, "USD minimum: transform, units or up axis incorrect.");
		require(std::abs(scene.mHeader.mMaximum[axis] - maximum[axis]) < 0.0001f, "USD maximum: transform, units or up axis incorrect.");
	}
	float area = 0;
	for (size_t index = 0; index < scene.mVertices.size(); index += 3)
	{
		const auto& a = scene.mVertices[index].mPosition;
		const auto& b = scene.mVertices[index + 1].mPosition;
		const auto& c = scene.mVertices[index + 2].mPosition;
		area += std::abs((b[0] - a[0]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[0] - a[0])) * 0.5f;
	}
	require(std::abs(area - 3.0f) < 0.0001f, "Concave polygon triangulation changed its area.");
	for (const auto& vertex : scene.mVertices)
	{
		require(vertex.mNormal[1] > 0.999f, "Authored normal was not transformed to Y-up.");
		require(std::abs(vertex.mColor[0] - 0.2f) < 0.0001f, "Indexed displayColor was lost.");
	}
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
		std::cout << "Scene file tests passed.\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
