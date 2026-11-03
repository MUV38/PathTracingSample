#pragma once

#include "CameraController.h"
#include "MeshRaytracer.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

class Renderer;

class UsdKitchen final
{
public:
	~UsdKitchen();
	void configure(const std::vector<std::wstring>& arguments);
	void initialize(Renderer& renderer);
	void update(Renderer& renderer, float deltaTime);
	void drawGui();
	void render(Renderer& renderer, ID3D12GraphicsCommandList* commands, ID3D12Resource* backBuffer);
	RayReconstruction& rayReconstruction() { return mRaytracer.rayReconstruction(); }
	bool readyForScreenshot() const
	{
		return mHasScene && !mConversionProcess && mRaytracer.accumulatedSamples() >= mScreenshotSamples
			&& mScreenshotGpuMilliseconds >= mScreenshotGpuBudget;
	}

private:
	void loadModel(const std::filesystem::path& path);
	void frameModel();
	void startConversion();
	void pollConversion();
	void cancelConversion();
	void readConversionLog();
	Renderer* mRenderer = nullptr;
	MeshRaytracer mRaytracer;
	Camera mCamera;
	CameraController mController;
	SceneHeader mHeader;
	std::vector<SceneMaterial> mMaterials;
	int mSelectedMaterial = 0;
	std::optional<float> mOverrideMetallic;
	std::optional<float> mOverrideRoughness;
	std::optional<float> mOverrideSpecular;
	size_t mTriangleCount = 0;
	size_t mMeshletCount = 0;
	DirectX::XMFLOAT3 mFocusPoint{};
	float mRadius = 1.0f;
	float mViewportLeft = 0.0f;
	float mExposure = 1.0f;
	Lighting mLighting;
	int mSelectedRectangle = 0;
	PathTracing mTracing;
	uint64_t mScreenshotSamples = 1;
	double mScreenshotGpuBudget = 0;
	double mScreenshotGpuMilliseconds = 0;
	std::filesystem::path mEnvironmentPath;
	std::string mEnvironmentName = "Built-in preview sky";
	bool mMeshletColors = false;
	bool mHasScene = false;
	bool mStartupConversion = false;
	bool mNeedsFrame = false;
	std::filesystem::path mModelPath;
	std::filesystem::path mStartupUsd;
	std::filesystem::path mConfiguredConverter;
	std::filesystem::path mConfiguredOutput;
	std::filesystem::path mGpuTimingPath;
	std::ofstream mGpuTimingLog;
	std::filesystem::path mPendingBinary;
	std::filesystem::path mLogPath;
	std::array<char, 4096> mUsdInput{};
	std::array<char, 4096> mConverterInput{};
	std::string mStatus;
	std::string mLog;
	HANDLE mConversionProcess = nullptr;
};
