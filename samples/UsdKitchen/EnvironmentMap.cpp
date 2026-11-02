#include "EnvironmentMap.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>

namespace
{
std::string line(std::ifstream& file)
{
	std::string text;
	for (char c; file.get(c);)
	{
		if (c == '\n')
		{
			if (!text.empty() && text.back() == '\r')
			{
				text.pop_back();
			}
			return text;
		}
		text.push_back(c);
		if (text.size() > 4096)
		{
			throw std::runtime_error("HDR header line is too long.");
		}
	}
	throw std::runtime_error("HDR header is truncated.");
}

uint8_t byte(std::ifstream& file)
{
	char value;
	if (!file.get(value))
	{
		throw std::runtime_error("HDR pixel data is truncated.");
	}
	return static_cast<uint8_t>(value);
}

std::array<uint8_t, 4> rgbe(std::ifstream& file)
{
	return {byte(file), byte(file), byte(file), byte(file)};
}
}

EnvironmentMap createPreviewEnvironment()
{
	// Self-authored linear HDR sky. It is sampled as a texture, including on primary misses.
	EnvironmentMap result{512, 256, {}};
	result.mPixels.resize(static_cast<size_t>(result.mWidth) * result.mHeight);
	constexpr float pi = 3.14159265359f;
	for (uint32_t y = 0; y < result.mHeight; ++y)
	{
		const float theta = (static_cast<float>(y) + 0.5f) / result.mHeight * pi;
		for (uint32_t x = 0; x < result.mWidth; ++x)
		{
			const float phi = ((static_cast<float>(x) + 0.5f) / result.mWidth - 0.5f) * 2.0f * pi;
			const std::array<float, 3> direction{std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi)};
			const float sky = std::pow((std::max)(direction[1], 0.0f), 0.4f);
			const float sun = std::pow((std::max)(direction[0] * 0.4f + direction[1] * 0.8f + direction[2] * 0.4472136f, 0.0f), 128.0f);
			const std::array<float, 3> horizon{0.8f, 0.85f, 0.95f}, zenith{0.15f, 0.35f, 0.8f}, ground{0.1f, 0.09f, 0.07f};
			auto& pixel = result.mPixels[static_cast<size_t>(y) * result.mWidth + x];
			for (size_t axis = 0; axis < 3; ++axis)
			{
				pixel[axis] = direction[1] >= 0.0f ? std::lerp(horizon[axis], zenith[axis], sky) + sun * 8.0f
					: std::lerp(horizon[axis], ground[axis], (std::min)(1.0f, -direction[1] * 5.0f));
			}
			pixel[3] = 1.0f;
		}
	}
	return result;
}

EnvironmentMap readRadianceEnvironment(const std::filesystem::path& path)
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
	{
		throw std::runtime_error("Cannot open the Radiance HDR environment.");
	}
	const auto signature = line(file);
	if (signature != "#?RADIANCE" && signature != "#?RGBE")
	{
		throw std::runtime_error("Environment must be a Radiance RGBE .hdr file.");
	}
	bool format = false, ended = false;
	for (int count = 0; count < 256; ++count)
	{
		const auto text = line(file);
		if (text.empty())
		{
			ended = true;
			break;
		}
		if (text.starts_with("FORMAT="))
		{
			if (text != "FORMAT=32-bit_rle_rgbe")
			{
				throw std::runtime_error("Only RGBE Radiance environments are supported (not XYZE).");
			}
			format = true;
		}
	}
	if (!format || !ended)
	{
		throw std::runtime_error("HDR FORMAT header is missing or too long.");
	}
	std::istringstream resolution(line(file));
	std::string yAxis, xAxis, extra;
	int64_t width = 0, height = 0;
	if (!(resolution >> yAxis >> height >> xAxis >> width) || (resolution >> extra)
		|| (yAxis != "-Y" && yAxis != "+Y") || (xAxis != "+X" && xAxis != "-X")
		|| width <= 0 || height <= 0 || width > 16384 || height > 16384 || width * height > 16777216)
	{
		throw std::runtime_error("HDR requires Y-major resolution, at most 16384 per axis / 16 million pixels.");
	}
	EnvironmentMap result{static_cast<uint32_t>(width), static_cast<uint32_t>(height), {}};
	result.mPixels.resize(static_cast<size_t>(width * height));
	std::vector<std::array<uint8_t, 4>> scanline(static_cast<size_t>(width));
	for (uint32_t y = 0; y < result.mHeight; ++y)
	{
		const auto first = rgbe(file);
		if (width >= 8 && width <= 32767 && first[0] == 2 && first[1] == 2 && (first[2] & 128) == 0)
		{
			if ((static_cast<uint32_t>(first[2]) << 8 | first[3]) != result.mWidth)
			{
				throw std::runtime_error("HDR scanline width mismatch.");
			}
			for (size_t channel = 0; channel < 4; ++channel)
			{
				for (uint32_t x = 0; x < result.mWidth;)
				{
					const uint8_t code = byte(file);
					const uint32_t count = code > 128 ? code - 128 : code;
					if (count == 0 || count > result.mWidth - x)
					{
						throw std::runtime_error("HDR RLE run exceeds the scanline.");
					}
					if (code > 128)
					{
						const uint8_t value = byte(file);
						for (uint32_t index = 0; index < count; ++index)
						{
							scanline[x++][channel] = value;
						}
					}
					else
					{
						for (uint32_t index = 0; index < count; ++index)
						{
							scanline[x++][channel] = byte(file);
						}
					}
				}
			}
		}
		else
		{
			scanline[0] = first;
			for (uint32_t x = 1; x < result.mWidth; ++x)
			{
				scanline[x] = rgbe(file);
			}
			for (const auto& pixel : scanline)
			{
				if (pixel[0] == 1 && pixel[1] == 1 && pixel[2] == 1)
				{
					throw std::runtime_error("Old Radiance repeat encoding is unsupported; export modern scanline RLE or flat RGBE.");
				}
			}
		}
		for (uint32_t x = 0; x < result.mWidth; ++x)
		{
			const auto& source = scanline[x];
			const uint32_t targetY = yAxis == "-Y" ? y : result.mHeight - 1 - y;
			const uint32_t targetX = xAxis == "+X" ? x : result.mWidth - 1 - x;
			auto& pixel = result.mPixels[static_cast<size_t>(targetY) * result.mWidth + targetX];
			const float scale = source[3] ? std::ldexp(1.0f, static_cast<int>(source[3]) - 136) : 0.0f;
			for (size_t channel = 0; channel < 3; ++channel)
			{
				pixel[channel] = source[channel] * scale;
			}
			pixel[3] = 1.0f;
		}
	}
	return result;
}

std::vector<std::array<float, 2>> createEnvironmentDistribution(const EnvironmentMap& environment)
{
	if (!environment.mWidth || !environment.mHeight || environment.mWidth > 16384 || environment.mHeight > 16384
		|| static_cast<uint64_t>(environment.mWidth) * environment.mHeight > 16777216
		|| environment.mPixels.size() != static_cast<size_t>(environment.mWidth) * environment.mHeight)
	{
		throw std::runtime_error("Invalid environment distribution dimensions.");
	}
	constexpr double pi = std::numbers::pi;
	const uint32_t width = environment.mWidth, height = environment.mHeight;
	std::vector<double> rows(height);
	double total = 0;
	for (uint32_t y = 0; y < height; ++y)
	{
		const double area = (2 * pi / width) * (std::cos(pi * y / height) - std::cos(pi * (y + 1) / height));
		for (uint32_t x = 0; x < width; ++x)
		{
			const auto& pixel = environment.mPixels[static_cast<size_t>(y) * width + x];
			for (float value : pixel)
			{
				if (!std::isfinite(value) || value < 0 || value > 1e20f)
				{
					throw std::runtime_error("Invalid environment distribution radiance.");
				}
			}
			rows[y] += (0.2126 * pixel[0] + 0.7152 * pixel[1] + 0.0722 * pixel[2]) * area;
		}
		total += rows[y];
	}
	std::vector<std::array<float, 2>> result(height + static_cast<size_t>(width) * height);
	double rowCdf = 0;
	float previousRow = 0;
	for (uint32_t y = 0; y < height; ++y)
	{
		const double area = (2 * pi / width) * (std::cos(pi * y / height) - std::cos(pi * (y + 1) / height));
		const double uniformRow = area * width / (4 * pi);
		const double rowMass = total > 0 ? 0.95 * rows[y] / total + 0.05 * uniformRow : uniformRow;
		rowCdf += rowMass;
		const float roundedRow = y + 1 == height ? 1.0f : static_cast<float>(rowCdf);
		result[y] = {roundedRow, 0};
		double columnCdf = 0;
		float previousColumn = 0;
		for (uint32_t x = 0; x < width; ++x)
		{
			const auto& pixel = environment.mPixels[static_cast<size_t>(y) * width + x];
			const double luminance = 0.2126 * pixel[0] + 0.7152 * pixel[1] + 0.0722 * pixel[2];
			const double mass = total > 0 ? 0.95 * luminance * area / total + 0.05 * area / (4 * pi) : area / (4 * pi);
			columnCdf += mass / rowMass;
			const float roundedColumn = x + 1 == width ? 1.0f : static_cast<float>(columnCdf);
			// Match the actual float CDF, including bins rounded to zero mass.
			const float density = static_cast<float>((roundedRow - previousRow) * (roundedColumn - previousColumn) / area);
			result[height + static_cast<size_t>(y) * width + x] = {roundedColumn, density};
			previousColumn = roundedColumn;
		}
		previousRow = roundedRow;
	}
	return result;
}
