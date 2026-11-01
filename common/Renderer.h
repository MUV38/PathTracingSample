#pragma once

#include "MouseMotion.h"

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <filesystem>
#include <string>

struct Sample;

class Renderer final
{
public:
	Renderer() = default;
	~Renderer();
	Renderer(const Renderer&) = delete;
	Renderer& operator=(const Renderer&) = delete;

	void initialize(HWND window, UINT width, UINT height, const Sample& sample);
	void resize(UINT width, UINT height);
	void render();
	void waitForGpu();
	void requestScreenshot(const std::filesystem::path& path = {});
	bool screenshotSucceeded() const { return mScreenshotSucceeded; }
	const std::string& screenshotStatus() const { return mScreenshotStatus; }
	const std::wstring& adapterName() const { return mAdapterName; }
	bool supportsRaytracing() const { return mRaytracingSupported; }
	ID3D12Device* device() const { return mDevice.Get(); }
	ID3D12CommandQueue* commandQueue() const { return mQueue.Get(); }
	UINT width() const { return mWidth; }
	UINT height() const { return mHeight; }
	const std::array<float, 4>& clearColor() const { return mClearColor; }
	void setMouseMotion(const MouseMotion& motion) { mMouseMotion = motion; }
	const MouseMotion& mouseMotion() const { return mMouseMotion; }

private:
	static constexpr UINT mBufferCount = 2;
	static constexpr UINT mGuiDescriptorCount = 64;
	template<typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;
	void createRenderTargets();
	void initializeGui(HWND window);
	void drawGui();
	void openScreenshotDirectory();
	void allocateGuiDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE* cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* gpuHandle);
	void freeGuiDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle);

	ComPtr<IDXGIFactory6> mFactory;
	ComPtr<ID3D12Device> mDevice;
	ComPtr<ID3D12CommandQueue> mQueue;
	ComPtr<IDXGISwapChain3> mSwapChain;
	ComPtr<ID3D12DescriptorHeap> mRtvHeap;
	ComPtr<ID3D12DescriptorHeap> mGuiSrvHeap;
	std::array<ComPtr<ID3D12Resource>, mBufferCount> mRenderTargets;
	ComPtr<ID3D12CommandAllocator> mAllocator;
	ComPtr<ID3D12GraphicsCommandList> mCommandList;
	ComPtr<ID3D12Fence> mFence;
	HANDLE mFenceEvent = nullptr;
	UINT64 mFenceValue = 0;
	UINT mRtvStride = 0;
	UINT mWidth = 0;
	UINT mHeight = 0;
	std::wstring mAdapterName;
	bool mRaytracingSupported = false;
	std::array<bool, mGuiDescriptorCount> mGuiDescriptorsInUse{};
	UINT mGuiDescriptorStride = 0;
	bool mGuiContextCreated = false;
	bool mGuiWin32Initialized = false;
	bool mGuiDx12Initialized = false;
	bool mShowDemoWindow = false;
	std::array<float, 4> mClearColor{0.035f, 0.075f, 0.14f, 1.0f};
	std::filesystem::path mScreenshotPath;
	std::string mScreenshotStatus;
	bool mScreenshotSucceeded = false;
	const Sample* mSample = nullptr;
	MouseMotion mMouseMotion;
};
