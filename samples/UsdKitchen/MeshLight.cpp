#include "MeshLight.h"

#include <cmath>
#include <limits>
#include <stdexcept>

std::vector<MeshLight> createMeshLights(std::span<const SceneVertex> vertices, std::span<const uint32_t> triangleIndices)
{
	if (triangleIndices.size() % 3 != 0)
	{
		throw std::runtime_error("Mesh light indices must contain complete triangles.");
	}
	std::vector<MeshLight> result;
	result.reserve(triangleIndices.size() / 3);
	for (size_t first = 0; first < triangleIndices.size(); first += 3)
	{
		for (size_t corner = 0; corner < 3; ++corner)
		{
			if (triangleIndices[first + corner] >= vertices.size())
			{
				throw std::runtime_error("Mesh light vertex index is out of range.");
			}
		}
		const auto& a = vertices[triangleIndices[first]];
		const auto& b = vertices[triangleIndices[first + 1]];
		const auto& c = vertices[triangleIndices[first + 2]];
		if (a.mMaterialIndex != b.mMaterialIndex || a.mMaterialIndex != c.mMaterialIndex)
		{
			throw std::runtime_error("A mesh light triangle must have one material.");
		}
		MeshLight light{};
		light.mPosition = a.mPosition;
		light.mMaterialIndex = a.mMaterialIndex;
		for (size_t axis = 0; axis < 3; ++axis)
		{
			light.mEdge1[axis] = b.mPosition[axis] - a.mPosition[axis];
			light.mEdge2[axis] = c.mPosition[axis] - a.mPosition[axis];
			if (!std::isfinite(light.mPosition[axis]) || !std::isfinite(light.mEdge1[axis]) || !std::isfinite(light.mEdge2[axis]))
			{
				throw std::runtime_error("Mesh light geometry is non-finite.");
			}
		}
		const auto& u = light.mEdge1;
		const auto& v = light.mEdge2;
		const double x = double(u[1]) * v[2] - double(u[2]) * v[1];
		const double y = double(u[2]) * v[0] - double(u[0]) * v[2];
		const double z = double(u[0]) * v[1] - double(u[1]) * v[0];
		const double area = 0.5 * std::sqrt(x * x + y * y + z * z);
		if (!std::isfinite(area) || area > std::numeric_limits<float>::max() * 0.5)
		{
			throw std::runtime_error("Mesh light triangle area is too large.");
		}
		light.mArea = static_cast<float>(area);
		result.push_back(light);
	}
	return result;
}

uint32_t updateMeshLightDistribution(std::span<MeshLight> lights, const std::vector<SceneMaterial>& materials)
{
	validateSceneMaterials(materials);
	double total = 0;
	uint32_t active = 0;
	for (auto& light : lights)
	{
		if (light.mMaterialIndex >= materials.size() || !std::isfinite(light.mArea) || light.mArea < 0)
		{
			throw std::runtime_error("Invalid mesh light material or area.");
		}
		const auto& material = materials[light.mMaterialIndex];
		for (size_t axis = 0; axis < 3; ++axis)
		{
			light.mRadiance[axis] = material.mEmissiveColor[axis] * material.mEmissiveStrength;
		}
		const auto& rgb = light.mRadiance;
		const double weight = light.mArea * (0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2]);
		total += weight;
		active += weight > 0 ? 1u : 0u;
	}
	double cumulative = 0;
	float previous = 0;
	for (auto& light : lights)
	{
		const auto& rgb = light.mRadiance;
		const double weight = light.mArea * (0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2]);
		cumulative += weight;
		light.mCdf = total > 0 ? static_cast<float>(cumulative / total) : 0;
		// Density agrees with the actual float CDF, including zero-mass bins.
		light.mAreaDensity = light.mArea > 0 ? (light.mCdf - previous) / light.mArea : 0;
		if (!std::isfinite(light.mAreaDensity))
		{
			throw std::runtime_error("Mesh light triangle is too small for a finite sampling PDF.");
		}
		previous = light.mCdf;
	}
	return active;
}
