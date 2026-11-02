#pragma once

#include "SceneFile.h"
#include "Camera.h"

#include <d3d12.h>
#include <wrl/client.h>

class Renderer;

class MeshRaytracer final
{
public:
	void initialize(Renderer& renderer);
	void load(Renderer& renderer, const SceneData& scene);
	void render(Renderer& renderer, const Camera& camera, float viewportLeft, bool headlight, float exposure, bool meshletColors,
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
	ComPtr<ID3D12DescriptorHeap> mSrvUavHeap;
	UINT mWidth = 0;
	UINT mHeight = 0;
};
