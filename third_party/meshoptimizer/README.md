# meshoptimizer

Upstream: https://github.com/zeux/meshoptimizer/tree/v1.3

Pinned release: **v1.3**, MIT License (see LICENSE.md).
Only allocator.cpp, indexgenerator.cpp, clusterizer.cpp and meshoptimizer.h are vendored.
Upstream source formatting is preserved. No package installation, CMake or runtime DLL is required.

The USD converter and SceneFileTests compile these sources directly. SampleFramework and the viewer do not link meshoptimizer.
Our integration is tools/UsdConverter/MeshletBuilder.cpp. It deduplicates the full vertex record and calls meshopt_buildMeshletsSpatial with 64 vertices, 32 minimum / 126 maximum triangles, fill_weight 0.5.
The maxima follow NVIDIA's mesh-shading recommendation: https://developer.nvidia.com/blog/introduction-turing-mesh-shaders/ . This is not a CLAS-specific performance guarantee.
The minimum triangle count is a clustering target; small or vertex-limited meshlets can contain fewer triangles.
