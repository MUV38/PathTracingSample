#include "MeshletBuilder.h"

#include "meshoptimizer.h"

#include <cmath>
#include <stdexcept>

void buildSceneMeshlets(SceneData& scene)
{
	const size_t indexCount = scene.mVertices.size();
	if (indexCount == 0 || indexCount % 3 != 0 || indexCount > 30000000)
	{
		throw std::runtime_error("Meshlet builder requires a nonempty triangle list of at most 30 million vertices.");
	}
	for (const auto& vertex : scene.mVertices)
	{
		for (size_t axis = 0; axis < 3; ++axis)
		{
			if (!std::isfinite(vertex.mPosition[axis]) || !std::isfinite(vertex.mNormal[axis]) || !std::isfinite(vertex.mColor[axis]))
			{
				throw std::runtime_error("Meshlet input contains non-finite vertex data.");
			}
		}
	}
	std::vector<unsigned int> remap(indexCount), indices(indexCount);
	const size_t vertexCount = meshopt_generateVertexRemap(remap.data(), nullptr, indexCount,
		scene.mVertices.data(), indexCount, sizeof(SceneVertex));
	std::vector<SceneVertex> vertices(vertexCount);
	meshopt_remapVertexBuffer(vertices.data(), scene.mVertices.data(), indexCount, sizeof(SceneVertex), remap.data());
	meshopt_remapIndexBuffer(indices.data(), nullptr, indexCount, remap.data());
	constexpr size_t maxVertices = sceneMeshletMaxVertices, maxTriangles = sceneMeshletMaxTriangles;
	const size_t bound = meshopt_buildMeshletsBound(indexCount, maxVertices, 32);
	std::vector<meshopt_Meshlet> meshlets(bound);
	std::vector<unsigned int> references(indexCount);
	std::vector<unsigned char> triangles(indexCount);
	const size_t count = meshopt_buildMeshletsSpatial(meshlets.data(), references.data(), triangles.data(),
		indices.data(), indexCount, vertices[0].mPosition.data(), vertexCount, sizeof(SceneVertex),
		maxVertices, 32, maxTriangles, 0.5f);
	SceneData result;
	result.mHeader = scene.mHeader;
	result.mHeader.mVersion = 2;
	result.mVertices = std::move(vertices);
	for (size_t index = 0; index < count; ++index)
	{
		const auto& meshlet = meshlets[index];
		result.mMeshlets.push_back({static_cast<uint32_t>(result.mMeshletVertices.size()),
			static_cast<uint32_t>(result.mMeshletTriangles.size()), meshlet.vertex_count, meshlet.triangle_count});
		result.mMeshletVertices.insert(result.mMeshletVertices.end(), references.begin() + meshlet.vertex_offset,
			references.begin() + meshlet.vertex_offset + meshlet.vertex_count);
		result.mMeshletTriangles.insert(result.mMeshletTriangles.end(), triangles.begin() + meshlet.triangle_offset,
			triangles.begin() + meshlet.triangle_offset + meshlet.triangle_count * 3);
	}
	scene = std::move(result);
}
