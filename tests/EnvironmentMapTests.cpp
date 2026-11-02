#include "EnvironmentMap.h"

#include <windows.h>

#include <cmath>
#include <fstream>
#include <iostream>
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
}

int wmain(int count, wchar_t** arguments)
{
	try
	{
		require(count == 2, "Usage: EnvironmentMapTests <test-output-directory>");
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
