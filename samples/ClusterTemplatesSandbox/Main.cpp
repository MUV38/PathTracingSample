#include "../ClasSandbox/ClasSandbox.h"
#include "ClusterTemplatesExperiment.h"

#include <cmath>

namespace
{
SceneData createPatchScene()
{
	constexpr uint32_t patchesPerAxis = 4;
	constexpr uint32_t cellsPerPatch = 4;
	constexpr uint32_t row = cellsPerPatch + 1;
	SceneData scene;
	scene.mHeader.mMinimum = {-2, -0.4f, -2};
	scene.mHeader.mMaximum = {2, 0.4f, 2};

	for (uint32_t tileZ = 0; tileZ < patchesPerAxis; ++tileZ)
	{
		for (uint32_t tileX = 0; tileX < patchesPerAxis; ++tileX)
		{
			scene.mMeshlets.push_back({
				static_cast<uint32_t>(scene.mMeshletVertices.size()),
				static_cast<uint32_t>(scene.mMeshletTriangles.size()),
				row * row, 2 * cellsPerPatch * cellsPerPatch
			});
			for (uint32_t z = 0; z < row; ++z)
			{
				for (uint32_t x = 0; x < row; ++x)
				{
					const float px = float(tileX) - 2.0f + float(x) / cellsPerPatch;
					const float pz = float(tileZ) - 2.0f + float(z) / cellsPerPatch;
					const float height = 0.4f * std::sin(2 * px) * std::cos(2 * pz);
					const std::array<float, 3> normal{
						-0.8f * std::cos(2 * px) * std::cos(2 * pz), 1,
						0.8f * std::sin(2 * px) * std::sin(2 * pz)
					};
					scene.mMeshletVertices.push_back(static_cast<uint32_t>(scene.mVertices.size()));
					scene.mVertices.push_back({{px, height, pz}, normal, {0.25f, 0.65f, 0.85f}});
				}
			}
			for (uint32_t z = 0; z < cellsPerPatch; ++z)
			{
				for (uint32_t x = 0; x < cellsPerPatch; ++x)
				{
					const auto a = static_cast<uint8_t>(z * row + x);
					const auto b = static_cast<uint8_t>(a + 1);
					const auto c = static_cast<uint8_t>(a + row);
					const auto d = static_cast<uint8_t>(c + 1);
					scene.mMeshletTriangles.insert(scene.mMeshletTriangles.end(), {a, c, b, b, c, d});
				}
			}
		}
	}
	scene.mHeader.mVertexCount = scene.mVertices.size();
	scene.mHeader.mMeshCount = 1;
	return scene;
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
	ClusterTemplatesExperiment experiment;
	const ClasSandboxOptions options{L"Cluster Templates Sandbox", "Cluster Templates Sandbox",
		L"Procedural terrain patches", createPatchScene};
	return runClasSandbox(instance, showCommand, options, bindClasExperiment(experiment));
}
