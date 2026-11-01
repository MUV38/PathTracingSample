#pragma once

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <string>

class Renderer final
{
public:
	Renderer() = default;
	~Renderer();
	Renderer(const Renderer&) = delete;
	Renderer& operator=(const Renderer&) = delete;

	void initialize(HWND window, UINT width, UINT height);
	void resize(UINT width, UINT height);
	void render();
	void waitForGpu();
	const std::wstring& adapterName() const { return mAdapterName; }
	bool supportsRaytracing() const { return mRaytracingSupported; }

private:
	static constexpr UINT mBufferCount = 2;
	static constexpr UINT mGuiDescriptorCount = 64;
	template<typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;
	void createRenderTargets();
	void initializeGui(HWND window);
	void drawGui();
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
};
