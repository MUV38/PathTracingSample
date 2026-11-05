#pragma once

#include "Camera.h"
#include "CameraController.h"
#include "ClasContext.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

struct ClasSandboxOptions
{
	const wchar_t* mWindowTitle;
	const char* mTitle;
	const wchar_t* mDefaultSceneName = nullptr;
	SceneData (*mCreateDefaultScene)() = nullptr;
	std::vector<std::array<float, 3>> mDefaultInstanceOffsets{};
};

struct ClasExperimentHooks
{
	std::function<void(const ClasContext&)> mInitialize;
	std::function<void(const ClasContext&, ID3D12GraphicsCommandList*)> mRecord;
	std::function<void()> mDrawGui;
	std::function<ClasTraceState()> mTraceState;
};

int runClasSandbox(HINSTANCE instance, int showCommand, const ClasSandboxOptions& options, const ClasExperimentHooks& experiment);

template<typename Experiment>
ClasExperimentHooks bindClasExperiment(Experiment& experiment)
{
	return {
		[&experiment](const ClasContext& context) { experiment.initialize(context); },
		[&experiment](const ClasContext& context, ID3D12GraphicsCommandList* commands) { experiment.record(context, commands); },
		[&experiment]() { experiment.drawGui(); },
		[&experiment]() { return experiment.traceState(); }
	};
}

class ClasSandbox final
{
public:
	ClasSandbox(const ClasSandboxOptions& options, const ClasExperimentHooks& experiment)
		: mOptions(options), mExperiment(experiment) {}
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
	bool mUseExperiment = false;
	std::filesystem::path mModelPath;
	SceneData mScene;
	ClasCapabilities mCapabilities;
	const ClasSandboxOptions& mOptions;
	const ClasExperimentHooks& mExperiment;
	std::vector<D3D12_RAYTRACING_INSTANCE_DESC> mReferenceInstances;
};
