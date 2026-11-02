#include "EnvironmentMap.h"

#include <windows.h>

#include <cmath>
#include <fstream>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>

namespace
{
void require(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

void writeHdr(const std::filesystem::path& path, const std::string& resolution, const std::vector<uint8_t>& pixels,
	const std::string& format = "32-bit_rle_rgbe")
{
	std::ofstream file(path, std::ios::binary);
	file << "#?RADIANCE\nFORMAT=" << format << "\n\n" << resolution << '\n';
	file.write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
	require(static_cast<bool>(file), "Cannot write HDR fixture.");
}

void expectRejected(const std::filesystem::path& path)
{
	try
	{
		readRadianceEnvironment(path);
	}
	catch (const std::runtime_error&)
	{
		return;
	}
	throw std::runtime_error("Invalid HDR was accepted.");
}

void testDistribution()
{
	constexpr double pi = std::numbers::pi;
	for (float level : {0.0f, 1.0f})
	{
		EnvironmentMap map{16, 8, std::vector<std::array<float,4>>(128, {level,level,level,1})};
		const auto distribution = createEnvironmentDistribution(map);
		double integral = 0;
		float previousRow = 0;
		for (uint32_t y = 0; y < map.mHeight; ++y)
		{
			const double area = 2 * pi / map.mWidth * (std::cos(pi * y / map.mHeight) - std::cos(pi * (y + 1) / map.mHeight));
			const float rowMass = distribution[y][0] - previousRow;
			require(rowMass > 0, "Row CDF is not strictly increasing.");
			float previousColumn = 0;
			for (uint32_t x = 0; x < map.mWidth; ++x)
			{
				const auto& cell = distribution[map.mHeight + y * map.mWidth + x];
				require(cell[0] > previousColumn, "Column CDF is not strictly increasing.");
				require(std::abs(cell[1] - 1 / (4 * pi)) < 1e-6, "Constant/black environment PDF must be uniform in solid angle.");
				require(std::abs(cell[1] * area - rowMass * (cell[0] - previousColumn)) < 1e-7, "PDF disagrees with CDF sampling probability.");
				integral += cell[1] * area;
				previousColumn = cell[0];
			}
			require(previousColumn == 1, "Column CDF must end at one.");
			previousRow = distribution[y][0];
		}
		require(previousRow == 1 && std::abs(integral - 1) < 1e-6, "Environment PDF is not normalized.");
	}
	EnvironmentMap bright{4, 2, std::vector<std::array<float,4>>(8, {0,0,0,1})};
	bright.mPixels[1] = {1000,1000,1000,1};
	const auto distribution = createEnvironmentDistribution(bright);
	require(std::abs(distribution[0][0] - 0.975f) < 1e-6, "Bright row importance is incorrect.");
	const float brightProbability = (distribution[3][0] - distribution[2][0]) * distribution[0][0];
	require(std::abs(brightProbability - 0.95625f) < 1e-6, "Bright texel importance is incorrect.");
	for (size_t index = bright.mHeight; index < distribution.size(); ++index)
	{
		require(distribution[index][1] > 0, "Uniform mixture must cover dark texels near bright texels.");
	}
	bright.mPixels[0][0] = -1;
	bool rejected = false;
	try
	{
		createEnvironmentDistribution(bright);
	}
	catch (const std::runtime_error&)
	{
		rejected = true;
	}
	require(rejected, "Distribution accepted invalid radiance.");
}
}

int wmain(int count, wchar_t** arguments)
{
	try
	{
		require(count == 2, "Usage: EnvironmentMapTests <test-output-directory>");
		testDistribution();
		const auto directory = std::filesystem::path(arguments[1]) / (L"environment-tests-" + std::to_wstring(GetCurrentProcessId()));
		require(std::filesystem::create_directories(directory), "Test directory already exists.");
		const auto rlePath = directory / L"constant.hdr";
		writeHdr(rlePath, "-Y 1 +X 8", {2,2,0,8, 136,128, 136,64, 136,32, 136,129});
		const auto rle = readRadianceEnvironment(rlePath);
		require(rle.mWidth == 8 && rle.mHeight == 1, "HDR resolution changed.");
		for (const auto& pixel : rle.mPixels)
		{
			require(pixel == std::array<float,4>{1,0.5f,0.25f,1}, "RGBE radiance conversion failed.");
		}
		const auto literalPath = directory / L"literal.hdr";
		writeHdr(literalPath, "-Y 1 +X 8", {2,2,0,8, 8,128,128,128,128,128,128,128,128, 136,64, 136,32, 136,129});
		require(readRadianceEnvironment(literalPath).mPixels == rle.mPixels, "RLE literal decoding failed.");
		const auto flatPath = directory / L"oriented.hdr";
		writeHdr(flatPath, "+Y 2 -X 2", {128,0,0,129, 0,128,0,129, 0,0,128,129, 128,128,128,129});
		const auto flat = readRadianceEnvironment(flatPath);
		require(flat.mPixels == std::vector<std::array<float,4>>{{1,1,1,1},{0,0,1,1},{0,1,0,1},{1,0,0,1}}, "HDR axis orientation failed.");
		const auto invalid = directory / L"invalid.hdr";
		writeHdr(invalid, "-Y 1 +X 8", {2,2,0,8,136});
		expectRejected(invalid);
		writeHdr(invalid, "-Y 1 +X 8", {2,2,0,8,137,128});
		expectRejected(invalid);
		writeHdr(invalid, "-Y 1 +X 8", {2,2,0,8,0});
		expectRejected(invalid);
		writeHdr(invalid, "-Y 1 +X 8", {2,2,0,7});
		expectRejected(invalid);
		writeHdr(invalid, "-Y 999999 +X 999999", {});
		expectRejected(invalid);
		writeHdr(invalid, "-X 1 +Y 1", {});
		expectRejected(invalid);
		writeHdr(invalid, "-Y 1 +X 1", {128,128,128,129}, "32-bit_rle_xyze");
		expectRejected(invalid);
		writeHdr(invalid, "-Y 1 +X 1", {1,1,1,1});
		expectRejected(invalid);
		const auto sky = createPreviewEnvironment();
		require(sky.mPixels.size() == static_cast<size_t>(sky.mWidth) * sky.mHeight, "Preview sky size mismatch.");
		bool hdr = false;
		for (const auto& pixel : sky.mPixels)
		{
			for (float value : pixel)
			{
				require(std::isfinite(value) && value >= 0, "Preview sky has invalid radiance.");
				hdr |= value > 1;
			}
		}
		require(hdr, "Preview sky should contain HDR radiance.");
		std::cout << "Environment tests passed. Fixture directory: " << directory.string() << '\n';
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
