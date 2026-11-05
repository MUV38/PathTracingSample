#pragma once

#include "SceneFile.h"

#include <d3d12.h>
#include <wrl/client.h>
#include <span>

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

// Borrowed resources: their owners must keep them alive through GPU completion.
// Table layout: three 64-byte records (raygen, miss, triangle hit group).
struct ClasTraceState
{
	ID3D12Resource* mTlas = nullptr;
	ID3D12StateObject* mPipeline = nullptr;
	ID3D12Resource* mShaderTable = nullptr;

	bool ready() const;
};

struct ClasContext
{
	Renderer& mRenderer;
	const SceneData& mScene;
	const MeshletGpuInputs& mGpu;
	const ClasCapabilities& mCapabilities;
	ID3D12RootSignature* mRootSignature;
	// Reference TLAS inputs, including the sandbox-owned BLAS addresses.
	std::span<const D3D12_RAYTRACING_INSTANCE_DESC> mReferenceInstances;
	ClasTraceState mReferenceTrace;
};
