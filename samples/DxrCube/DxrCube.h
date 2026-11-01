#pragma once

#include "Camera.h"
#include "CameraController.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <array>

class Renderer;

class DxrCube final
{
public:
	void initialize(Renderer& renderer);
	void update(Renderer& renderer, float deltaTime);
	void drawGui();
	void render(Renderer& renderer, ID3D12GraphicsCommandList* commandList, ID3D12Resource* backBuffer);

private:
	template<typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;
	void buildAccelerationStructures(Renderer& renderer);
	void createPipeline();
	void createShaderTable();
	void resizeOutput(UINT width, UINT height);

	ComPtr<ID3D12Device5> mDevice;
	ComPtr<ID3D12Resource> mVertices;
	ComPtr<ID3D12Resource> mIndices;
	ComPtr<ID3D12Resource> mBlas;
	ComPtr<ID3D12Resource> mTlas;
	ComPtr<ID3D12RootSignature> mRootSignature;
	ComPtr<ID3D12StateObject> mPipeline;
	ComPtr<ID3D12Resource> mShaderTable;
	ComPtr<ID3D12Resource> mOutput;
	ComPtr<ID3D12DescriptorHeap> mSrvUavHeap;
	UINT mWidth = 0;
	UINT mHeight = 0;
	Camera mCamera;
	CameraController mCameraController;
	float mViewportLeft = 0.0f;
	std::array<float, 3> mAlbedo{0.2f, 0.62f, 0.95f};
};
