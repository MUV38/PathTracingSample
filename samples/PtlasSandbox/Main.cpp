#include "../ClasSandbox/ClasSandbox.h"
#include "PtlasExperiment.h"

namespace
{
SceneData createBoxScene()
{
	SceneData scene;
	scene.mHeader.mMinimum = {-0.35f, 0.0f, -0.35f};
	scene.mHeader.mMaximum = {0.35f, 0.7f, 0.35f};
	constexpr std::array<std::array<float, 3>, 8> positions{{
		{-0.35f, 0.0f, -0.35f}, {0.35f, 0.0f, -0.35f}, {0.35f, 0.7f, -0.35f}, {-0.35f, 0.7f, -0.35f},
		{-0.35f, 0.0f, 0.35f}, {0.35f, 0.0f, 0.35f}, {0.35f, 0.7f, 0.35f}, {-0.35f, 0.7f, 0.35f}
	}};
	constexpr std::array<std::array<uint32_t, 4>, 6> faces{{
		{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 4, 7, 3},
		{1, 2, 6, 5}, {0, 1, 5, 4}, {3, 7, 6, 2}
	}};
	constexpr std::array<std::array<float, 3>, 6> normals{{
		{0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}
	}};
	for (size_t i = 0; i < faces.size(); ++i)
	{
		const auto base = static_cast<uint8_t>(scene.mVertices.size());
		for (uint32_t corner : faces[i])
		{
			scene.mMeshletVertices.push_back(static_cast<uint32_t>(scene.mVertices.size()));
			scene.mVertices.push_back({positions[corner], normals[i], {0.30f, 0.65f, 0.9f}});
		}
		const auto b = static_cast<uint8_t>(base + 1);
		const auto c = static_cast<uint8_t>(base + 2);
		const auto d = static_cast<uint8_t>(base + 3);
		scene.mMeshletTriangles.insert(scene.mMeshletTriangles.end(), {base, b, c, base, c, d});
	}
	scene.mMeshlets.push_back({0, 0, 24, 12});
	scene.mHeader.mVertexCount = scene.mVertices.size();
	scene.mHeader.mMeshCount = 1;
	return scene;
}

std::vector<std::array<float, 3>> createInstanceOffsets()
{
	std::vector<std::array<float, 3>> offsets;
	// Consecutive groups of four instance IDs form one spatial group.
	for (float groupZ : {-2.2f, 2.2f})
	{
		for (float groupX : {-2.2f, 2.2f})
		{
			for (float z : {-0.55f, 0.55f})
			{
				for (float x : {-0.55f, 0.55f})
				{
					offsets.push_back({groupX + x, 0.0f, groupZ + z});
				}
			}
		}
	}
	return offsets;
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
	PtlasExperiment experiment;
	const ClasSandboxOptions options{L"PTLAS Sandbox", "PTLAS Sandbox",
		L"Procedural box groups", createBoxScene, createInstanceOffsets()};
	return runClasSandbox(instance, showCommand, options, bindClasExperiment(experiment));
}
