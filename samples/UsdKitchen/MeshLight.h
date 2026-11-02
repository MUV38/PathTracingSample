#pragma once

#include "SceneFile.h"

#include <span>

// Matches MeshLight in MeshLight.hlsli, in meshlet / triangle order.
struct MeshLight
{
	std::array<float,3> mPosition;
	float mArea = 0;
	std::array<float,3> mEdge1;
	float mCdf = 0;
	std::array<float,3> mEdge2;
	float mAreaDensity = 0;
	std::array<float,3> mRadiance{};
	uint32_t mMaterialIndex = 0;
};
static_assert(sizeof(MeshLight) == 64);

std::vector<MeshLight> createMeshLights(std::span<const SceneVertex> vertices, std::span<const uint32_t> triangleIndices);
uint32_t updateMeshLightDistribution(std::span<MeshLight> lights, const std::vector<SceneMaterial>& materials);
