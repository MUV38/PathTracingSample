#pragma once

#include "SceneFile.h"
#include "Camera.h"
#include "Lighting.h"
#include "EnvironmentMap.h"
#include "PathTracing.h"
#include "MeshLight.h"
#include "RayReconstruction.h"

#include <d3d12.h>
#include <array>
#include <wrl/client.h>

class Renderer;

class MeshRaytracer final
{
public:
	void initialize(Renderer& renderer);
	void load(Renderer& renderer, const SceneData& scene);
	void loadEnvironment(Renderer& renderer, const EnvironmentMap& environment);
	uint32_t emissiveTriangleCount() const { return mEmissiveTriangleCount; }
	void updateMaterials(Renderer& renderer, const std::vector<SceneMaterial>& materials);
	void resetAccumulation() { mResetAccumulation = true; }
	uint64_t accumulatedSamples() const { return static_cast<uint64_t>(mAccumulatedFrames) * mLastSamplesPerFrame; }
	double gpuMilliseconds() const { return mGpuMilliseconds; }
	bool giHistoryUsedLastFrame() const { return mLastGiHistoryValid; }
	RayReconstruction& rayReconstruction() { return mRayReconstruction; }
	bool rayReconstructionEnabled() const { return mUseRayReconstruction; }
	void setRayReconstructionEnabled(bool enabled) { mUseRayReconstruction = enabled; }
	uint32_t rayReconstructionView() const { return mRRView; }
	void setRayReconstructionView(uint32_t view) { mRRView = view; }
	void render(Renderer& renderer, const Camera& camera, float viewportLeft, const Lighting& lighting, const PathTracing& tracing, float exposure, bool meshletColors,
		ID3D12GraphicsCommandList* commandList, ID3D12Resource* backBuffer);

private:
	template<typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;
	void createPipeline();
	void createShaderTable();
	void resizeOutput(UINT width, UINT height);
	void resizeRestir(size_t pixels);
	// Keep the Streamline module alive until every graphics object is released.
	RayReconstruction mRayReconstruction;
	ComPtr<ID3D12Device5> mDevice;
	ComPtr<ID3D12Resource> mVertices;
	ComPtr<ID3D12Resource> mMaterials;
	size_t mMaterialCount = 0;
	std::vector<SceneMaterial> mMaterialData;
	std::vector<MeshLight> mMeshLightData;
	uint32_t mEmissiveTriangleCount = 0;
	ComPtr<ID3D12Resource> mMeshLights;
	ComPtr<ID3D12Resource> mIndices;
	ComPtr<ID3D12Resource> mMeshlets;
	ComPtr<ID3D12Resource> mMeshletVertices;
	ComPtr<ID3D12Resource> mMeshletTriangles;
	ComPtr<ID3D12Resource> mBlas;
	ComPtr<ID3D12Resource> mTlas;
	ComPtr<ID3D12RootSignature> mRootSignature;
	ComPtr<ID3D12StateObject> mPipeline;
	ComPtr<ID3D12Resource> mShaderTable;
	ComPtr<ID3D12Resource> mOutput;
	ComPtr<ID3D12Resource> mAccumulation;
	ComPtr<ID3D12Resource> mEnvironment;
	ComPtr<ID3D12Resource> mEnvironmentDistribution;
	ComPtr<ID3D12Resource> mConstants;
	ComPtr<ID3D12Resource> mGgxEnergy;
	ComPtr<ID3D12DescriptorHeap> mSrvUavHeap;
	UINT mWidth = 0;
	UINT mHeight = 0;
	uint32_t mAccumulatedFrames = 0;
	uint32_t mLastSamplesPerFrame = 1;
	bool mResetAccumulation = true;
	std::vector<uint8_t> mPreviousConstants;
	std::array<ComPtr<ID3D12Resource>,5> mRestirBuffers;
	ComPtr<ID3D12Resource> mGiCandidates;
	ComPtr<ID3D12Resource> mPreviousGiCandidates;
	size_t mGiPixels = 0;
	size_t mPreviousGiPixels = 0;
	bool mGiHistoryValid = false;
	bool mLastGiHistoryValid = false;
	size_t mRestirPixels = 0;
	bool mRestirHistoryValid = false;
	uint32_t mRestirFrame = 0;
	DirectX::XMFLOAT4X4 mPreviousViewProjection{};
	float mPreviousViewportLeft = 0;
	ComPtr<ID3D12QueryHeap> mTimestampHeap;
	ComPtr<ID3D12Resource> mTimestampReadback;
	uint64_t mTimestampFrequency = 0;
	bool mHasTimestamp = false;
	double mGpuMilliseconds = -1;
	bool mUseRayReconstruction = false;
	bool mRRHistoryValid = false;
	uint32_t mRRView = 0;
};
