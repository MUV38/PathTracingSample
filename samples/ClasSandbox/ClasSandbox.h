#pragma once

#include "Camera.h"
#include "CameraController.h"
#include "ClasExperiment.h"

#include <filesystem>
#include <string>
#include <vector>

class ClasSandbox final
{
public:
	void configure(const std::vector<std::wstring>& arguments);
	void initialize(Renderer& renderer);
	void update(Renderer& renderer, float deltaTime);
	void drawGui();
	void render(Renderer& renderer, ID3D12GraphicsCommandList* commands, ID3D12Resource* backBuffer);

private:
	template<typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;
	void buildAccelerationStructures(Renderer& renderer);
	void createPipeline();
	void createShaderTable();
	void resizeOutput(UINT width, UINT height);
	void frameModel();
	ClasContext context(Renderer& renderer);

	ComPtr<ID3D12Device5> mDevice;
	MeshletGpuInputs mGpu;
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
	DirectX::XMFLOAT3 mFocus{};
	float mRadius = 1.0f;
	bool mNeedsFrame = true;
	bool mMeshletColors = true;
	bool mUseClas = false;
	std::filesystem::path mModelPath;
	SceneData mScene;
	ClasCapabilities mCapabilities;
	ClasExperiment mExperiment;
};
