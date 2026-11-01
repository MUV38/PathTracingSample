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
	template<typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;
	void createRenderTargets();

	ComPtr<IDXGIFactory6> mFactory;
	ComPtr<ID3D12Device> mDevice;
	ComPtr<ID3D12CommandQueue> mQueue;
	ComPtr<IDXGISwapChain3> mSwapChain;
	ComPtr<ID3D12DescriptorHeap> mRtvHeap;
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
};
