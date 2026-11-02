#pragma once

#include "SceneFile.h"
#include "Camera.h"
#include "Lighting.h"
#include "EnvironmentMap.h"
#include "PathTracing.h"

#include <d3d12.h>
#include <wrl/client.h>

class Renderer;

class MeshRaytracer final
{
public:
	void initialize(Renderer& renderer);
	void load(Renderer& renderer, const SceneData& scene);
	void loadEnvironment(Renderer& renderer, const EnvironmentMap& environment);
	void resetAccumulation() { mResetAccumulation = true; }
	uint64_t accumulatedSamples() const { return static_cast<uint64_t>(mAccumulatedFrames) * mLastSamplesPerFrame; }
	void render(Renderer& renderer, const Camera& camera, float viewportLeft, const Lighting& lighting, const PathTracing& tracing, float exposure, bool meshletColors,
		ID3D12GraphicsCommandList* commandList, ID3D12Resource* backBuffer);

private:
	template<typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;
	void createPipeline();
	void createShaderTable();
	void resizeOutput(UINT width, UINT height);
	ComPtr<ID3D12Device5> mDevice;
	ComPtr<ID3D12Resource> mVertices;
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
	ComPtr<ID3D12DescriptorHeap> mSrvUavHeap;
	UINT mWidth = 0;
	UINT mHeight = 0;
	uint32_t mAccumulatedFrames = 0;
	uint32_t mLastSamplesPerFrame = 1;
	bool mResetAccumulation = true;
	std::vector<uint8_t> mPreviousConstants;
};
