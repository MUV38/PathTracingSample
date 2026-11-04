#include "../samples/UsdKitchen/MeshLight.h"

#include <cmath>
#include <filesystem>
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
template<typename F> void expectRejected(F action)
{
	bool rejected = false;
	try
	{
		action();
	}
	catch (const std::runtime_error&)
	{
		rejected = true;
	}
	require(rejected, "Invalid mesh light input was accepted.");
}
void testDistribution()
{
	std::vector<SceneVertex> vertices{
		{{0,0,0},{0,0,1},{1,1,1},0}, {{1,0,0},{0,0,1},{1,1,1},0}, {{0,1,0},{0,0,1},{1,1,1},0},
		{{2,0,0},{0,0,1},{1,1,1},1}, {{4,0,0},{0,0,1},{1,1,1},1}, {{2,1,0},{0,0,1},{1,1,1},1},
		{{0,0,1},{0,0,1},{1,1,1},2}, {{1,0,1},{0,0,1},{1,1,1},2}, {{0,1,1},{0,0,1},{1,1,1},2}};
	const std::vector<uint32_t> indices{0,1,2,3,4,5,6,7,8,3,3,3};
	auto lights = createMeshLights(vertices, indices);
	require(lights.size() == 4 && lights[0].mArea == 0.5f && lights[1].mArea == 1 && lights[3].mArea == 0, "Triangle geometry/area changed.");
	std::vector<SceneMaterial> materials(3);
	materials[0].mEmissiveColor = materials[1].mEmissiveColor = {1,1,1};
	materials[1].mEmissiveStrength = 2;
	require(updateMeshLightDistribution(lights, materials) == 2, "Emitter count includes dark/degenerate triangles.");
	require(std::abs(lights[0].mCdf - 0.2f) < 1e-6f && lights[1].mCdf == 1 && lights[2].mCdf == 1, "Area/radiance importance is incorrect.");
	float previous = 0, integral = 0;
	for (const auto& light : lights)
	{
		require(light.mCdf >= previous && std::isfinite(light.mAreaDensity), "CDF/PDF is invalid.");
		require(std::abs(light.mAreaDensity * light.mArea - (light.mCdf - previous)) < 1e-6f, "CDF and area PDF disagree.");
		integral += light.mAreaDensity * light.mArea;
		previous = light.mCdf;
	}
	require(std::abs(integral - 1) < 1e-6f, "Mesh light probabilities do not sum to one.");
	materials[0].mEmissiveStrength = 0;
	require(updateMeshLightDistribution(lights, materials) == 1 && lights[0].mCdf == 0 && lights[0].mAreaDensity == 0, "Turning an emitter off left stale probabilities.");
	materials[1].mEmissiveStrength = 0;
	require(updateMeshLightDistribution(lights, materials) == 0, "All-dark mesh still has emitters.");
	for (const auto& light : lights)
	{
		require(light.mCdf == 0 && light.mAreaDensity == 0, "All-dark distribution must be zero.");
	}
	materials[0].mEmissiveStrength = materials[1].mEmissiveStrength = 1;
	materials[0].mEmissiveColor = {1,0,0};
	materials[2].mEmissiveColor = {0,0,1};
	materials[2].mEmissiveStrength = 4;
	require(updateMeshLightDistribution(lights, materials) == 3, "Enabling a formerly dark material was not reflected.");
	const float redProbability = static_cast<float>((0.5 * 0.2126) / (0.5 * 0.2126 + 1 + 0.5 * 4 * 0.0722));
	require(std::abs(lights[0].mCdf - redProbability) < 1e-6f, "Emission color did not update luminance weights.");
	materials[0].mEmissiveColor[0] = -1;
	expectRejected([&] { updateMeshLightDistribution(lights, materials); });
	expectRejected([&] { createMeshLights(vertices, std::vector<uint32_t>{99,0,1}); });
	expectRejected([&] { createMeshLights(vertices, std::vector<uint32_t>{0,1}); });
	expectRejected([&] { createMeshLights(vertices, std::vector<uint32_t>{0,1,3}); });
	vertices[0].mPosition[0] = std::numeric_limits<float>::quiet_NaN();
	expectRejected([&] { createMeshLights(vertices, std::vector<uint32_t>{0,1,2}); });
}
void quad(SceneData& scene, std::array<float,3> a, std::array<float,3> b, std::array<float,3> c, std::array<float,3> d,
	std::array<float,3> normal, uint32_t material)
{
	const auto vertexOffset = static_cast<uint32_t>(scene.mMeshletVertices.size());
	const auto triangleOffset = static_cast<uint32_t>(scene.mMeshletTriangles.size());
	for (const auto& position : {a,b,c,d})
	{
		scene.mMeshletVertices.push_back(static_cast<uint32_t>(scene.mVertices.size()));
		scene.mVertices.push_back({position, normal, {1,1,1}, material});
	}
	for (uint8_t index : std::array<uint8_t,6>{0,2,1,0,3,2})
	{
		scene.mMeshletTriangles.push_back(index);
	}
	scene.mMeshlets.push_back({vertexOffset,triangleOffset,4,2});
}
SceneData fixture(bool blocker, bool mirror, bool multiple, bool dark)
{
	SceneData scene;
	scene.mHeader.mMeshCount = 1;
	scene.mMaterials.resize(3);
	scene.mMaterials[0].mSpecular = 0;
	if (mirror)
	{
		scene.mMaterials[0].mMetallic = 1;
		scene.mMaterials[0].mRoughness = 0;
	}
	for (size_t index = 1; index < 3; ++index)
	{
		scene.mMaterials[index].mBaseColor = {0,0,0};
		scene.mMaterials[index].mSpecular = 0;
		scene.mMaterials[index].mEmissiveColor = {1,1,1};
		scene.mMaterials[index].mEmissiveStrength = dark ? 0.0f : 4.0f;
	}
	quad(scene, {-2,0,-2}, {2,0,-2}, {2,0,2}, {-2,0,2}, {0,1,0}, 0);
	if (blocker)
	{
		quad(scene, {-0.4f,1,-0.4f}, {0.4f,1,-0.4f}, {0.4f,1,0.4f}, {-0.4f,1,0.4f}, {0,1,0}, 0);
		quad(scene, {-0.4f,0,-0.4f}, {0.4f,0,-0.4f}, {0.4f,1,-0.4f}, {-0.4f,1,-0.4f}, {0,0,-1}, 0);
		quad(scene, {-0.4f,0,0.4f}, {0.4f,0,0.4f}, {0.4f,1,0.4f}, {-0.4f,1,0.4f}, {0,0,1}, 0);
		quad(scene, {-0.4f,0,-0.4f}, {-0.4f,0,0.4f}, {-0.4f,1,0.4f}, {-0.4f,1,-0.4f}, {-1,0,0}, 0);
		quad(scene, {0.4f,0,-0.4f}, {0.4f,0,0.4f}, {0.4f,1,0.4f}, {0.4f,1,-0.4f}, {1,0,0}, 0);
	}
	if (multiple)
	{
		scene.mMaterials[1].mEmissiveColor = {1,0.15f,0.05f};
		scene.mMaterials[2].mEmissiveColor = {0.05f,0.2f,1};
		scene.mMaterials[1].mEmissiveStrength = scene.mMaterials[2].mEmissiveStrength = 12;
		quad(scene, {-1.5f,2,-0.8f}, {-0.3f,2,-0.8f}, {-0.3f,2,0.8f}, {-1.5f,2,0.8f}, {0,-1,0}, 1);
		quad(scene, {0.3f,2,-0.8f}, {1.5f,2,-0.8f}, {1.5f,2,0.8f}, {0.3f,2,0.8f}, {0,-1,0}, 2);
	}
	else
	{
		quad(scene, {-1,2,-1}, {1,2,-1}, {1,2,1}, {-1,2,1}, {0,-1,0}, 1);
	}
	return scene;
}
}
int wmain(int count, wchar_t** arguments)
{
	try
	{
		require(count == 2, "Usage: MeshLightTests <output-directory>");
		testDistribution();
		const std::filesystem::path directory = arguments[1];
		std::filesystem::create_directories(directory);
		for (const auto& name : {L"single", L"shadow", L"multiple", L"mirror", L"dark", L"gi-colored", L"gi-rough-metal", L"pt-hybrid-mirror", L"pt-hybrid-glossy"})
		{
			const std::wstring mode = name;
			const bool roughMetal = mode == L"gi-rough-metal";
			const bool hybrid = mode == L"pt-hybrid-mirror" || mode == L"pt-hybrid-glossy";
			const bool gi = mode == L"gi-colored" || roughMetal;
			auto scene = fixture(mode == L"shadow" || gi, mode == L"mirror" || hybrid, mode == L"multiple" || gi || hybrid, mode == L"dark");
			if (hybrid)
			{
				scene.mMaterials[0].mRoughness = mode == L"pt-hybrid-glossy" ? 0.12f : 0.0f;
				scene.mMaterials.emplace_back();
				scene.mMaterials[3].mSpecular = 0;
				quad(scene, {-2,0,-2}, {-2,2,-2}, {2,2,-2}, {2,0,-2}, {0,0,1}, 3);
				quad(scene, {-2,0,2}, {-2,2,2}, {-2,2,-2}, {-2,0,-2}, {1,0,0}, 3);
			}
			if (gi)
			{
				// A GGX+diffuse primary floor, Lambertian secondary blocker, two colored emitters.
				scene.mMaterials.push_back(scene.mMaterials[0]);
				scene.mMaterials[0].mSpecular = 0.5f;
				if (roughMetal)
				{
					scene.mMaterials[0].mMetallic = 1;
					scene.mMaterials[0].mRoughness = 0.6f;
				}
				for (size_t meshletIndex = 1; meshletIndex <= 5; ++meshletIndex)
				{
					const auto& meshlet = scene.mMeshlets[meshletIndex];
					for (uint32_t index = 0; index < meshlet.mVertexCount; ++index)
					{
						scene.mVertices[scene.mMeshletVertices[meshlet.mVertexOffset + index]].mMaterialIndex = 3;
					}
				}
			}
			const auto path = directory / (mode + L".ptscene");
			writeSceneFile(path, scene);
			const auto loaded = readSceneFile(path);
			auto lights = createMeshLights(loaded.mVertices, meshletTriangleIndices(loaded));
			const uint32_t expected = mode == L"dark" ? 0 : (mode == L"multiple" || gi || hybrid) ? 4 : 2;
			require(updateMeshLightDistribution(lights, loaded.mMaterials) == expected, "Fixture lost its emissive triangles.");
		}
		std::cout << "Mesh light CPU tests passed; fixtures: " << directory.string() << '\n';
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
