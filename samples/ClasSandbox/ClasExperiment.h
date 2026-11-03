#pragma once

#include "SceneFile.h"

#include <d3d12.h>
#include <wrl/client.h>

class Renderer;

struct ClasCapabilities
{
	HRESULT mEnablePreview = E_PENDING;
	HRESULT mFeatureQuery = E_NOINTERFACE;
	HRESULT mDeviceQuery = E_NOINTERFACE;
	HRESULT mCommandListQuery = E_NOINTERFACE;
	HRESULT mShaderModelQuery = E_NOINTERFACE;
	bool mClustersSupported = false;
	bool mShaderModel610 = false;

	bool available() const;
};

// Owned by ClasSandbox for the sample's lifetime. Offsets/strides match SceneFile.h.
// All buffers are DEFAULT heap resources in NON_PIXEL_SHADER_RESOURCE state.
struct MeshletGpuInputs
{
	Microsoft::WRL::ComPtr<ID3D12Resource> mVertices;
	Microsoft::WRL::ComPtr<ID3D12Resource> mMeshlets;
	Microsoft::WRL::ComPtr<ID3D12Resource> mVertexReferences;
	Microsoft::WRL::ComPtr<ID3D12Resource> mTriangleBytes;
	Microsoft::WRL::ComPtr<ID3D12Resource> mMaterials;
};

struct ClasContext
{
	Renderer& mRenderer;
	const SceneData& mScene;
	const MeshletGpuInputs& mGpu;
	const ClasCapabilities& mCapabilities;
	ID3D12RootSignature* mRootSignature;
};

// Borrowed resources: keep their owners in ClasExperiment until GPU completion.
// Table layout: three 64-byte records (raygen, miss, triangle hit group).
struct ClasTraceState
{
	ID3D12Resource* mTlas = nullptr;
	ID3D12StateObject* mPipeline = nullptr;
	ID3D12Resource* mShaderTable = nullptr;

	bool ready() const;
};

// User implementation boundary. No CLAS build/assembly/shading is implemented.
class ClasExperiment final
{
public:
	void initialize(const ClasContext& context);
	void record(const ClasContext& context, ID3D12GraphicsCommandList* commands);
	void drawGui();
	ClasTraceState traceState() const;
};
