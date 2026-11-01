#include "Renderer.h"

#include <cstdio>
#include <stdexcept>

namespace
{
void check(HRESULT result, const char* operation)
{
	if (FAILED(result))
	{
		char message[256]{};
		sprintf_s(message, "%s failed (HRESULT 0x%08X).", operation, static_cast<unsigned>(result));
		throw std::runtime_error(message);
	}
}
}

Renderer::~Renderer()
{
	// Destructors must still release the event if the device has been removed.
	try { waitForGpu(); }
	catch (...) { OutputDebugStringW(L"GPU wait failed during shutdown.\n"); }
	if (mFenceEvent)
	{
		CloseHandle(mFenceEvent);
	}
}

void Renderer::initialize(HWND window, UINT width, UINT height)
{
	UINT factoryFlags = 0;
#ifdef _DEBUG
	ComPtr<ID3D12Debug> debug;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
	{
		debug->EnableDebugLayer();
		factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
	}
	else
	{
		OutputDebugStringW(L"D3D12 debug layer unavailable; continuing without validation.\n");
	}
#endif
	check(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&mFactory)), "CreateDXGIFactory2");

	// Prefer a hardware adapter; software rendering remains useful for the clear-only milestone.
	ComPtr<IDXGIAdapter1> adapter;
	for (UINT index = 0; ; ++index)
	{
		const HRESULT result = mFactory->EnumAdapterByGpuPreference(
			index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));
		if (result == DXGI_ERROR_NOT_FOUND)
		{
			break;
		}
		check(result, "EnumAdapterByGpuPreference");
		DXGI_ADAPTER_DESC1 description{};
		check(adapter->GetDesc1(&description), "GetDesc1");
		if (!(description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
			SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&mDevice))))
		{
			mAdapterName = description.Description;
			break;
		}
		adapter.Reset();
	}
	if (!mDevice)
	{
		check(mFactory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)), "EnumWarpAdapter");
		check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&mDevice)), "D3D12CreateDevice (WARP)");
		mAdapterName = L"WARP (software)";
	}

	D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};
	mRaytracingSupported = SUCCEEDED(mDevice->CheckFeatureSupport(
		D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options))) &&
		options.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;

	D3D12_COMMAND_QUEUE_DESC queueDescription{};
	queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	check(mDevice->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&mQueue)), "CreateCommandQueue");
	check(mDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence)), "CreateFence");
	mFenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	if (!mFenceEvent)
	{
		check(HRESULT_FROM_WIN32(GetLastError()), "CreateEventW");
	}

	DXGI_SWAP_CHAIN_DESC1 swapDescription{};
	swapDescription.Width = width;
	swapDescription.Height = height;
	swapDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapDescription.SampleDesc.Count = 1;
	swapDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapDescription.BufferCount = mBufferCount;
	swapDescription.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	ComPtr<IDXGISwapChain1> swapChain;
	check(mFactory->CreateSwapChainForHwnd(mQueue.Get(), window, &swapDescription,
		nullptr, nullptr, &swapChain), "CreateSwapChainForHwnd");
	check(swapChain.As(&mSwapChain), "Query IDXGISwapChain3");
	check(mFactory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER), "MakeWindowAssociation");

	D3D12_DESCRIPTOR_HEAP_DESC heapDescription{};
	heapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	heapDescription.NumDescriptors = mBufferCount;
	check(mDevice->CreateDescriptorHeap(&heapDescription, IID_PPV_ARGS(&mRtvHeap)), "CreateDescriptorHeap");
	mRtvStride = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	createRenderTargets();
	check(mDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&mAllocator)), "CreateCommandAllocator");
	check(mDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, mAllocator.Get(),
		nullptr, IID_PPV_ARGS(&mCommandList)), "CreateCommandList");
	check(mCommandList->Close(), "Close initial command list");
	mWidth = width;
	mHeight = height;
}

void Renderer::createRenderTargets()
{
	auto handle = mRtvHeap->GetCPUDescriptorHandleForHeapStart();
	for (UINT index = 0; index < mBufferCount; ++index)
	{
		check(mSwapChain->GetBuffer(index, IID_PPV_ARGS(&mRenderTargets[index])), "GetBuffer");
		mDevice->CreateRenderTargetView(mRenderTargets[index].Get(), nullptr, handle);
		handle.ptr += mRtvStride;
	}
}

void Renderer::waitForGpu()
{
	if (!mQueue || !mFence || !mFenceEvent)
	{
		return;
	}
	const UINT64 value = ++mFenceValue;
	check(mQueue->Signal(mFence.Get(), value), "Signal fence");
	if (mFence->GetCompletedValue() < value)
	{
		check(mFence->SetEventOnCompletion(value, mFenceEvent), "SetEventOnCompletion");
		if (WaitForSingleObject(mFenceEvent, INFINITE) != WAIT_OBJECT_0)
		{
			throw std::runtime_error("Waiting for the GPU fence failed.");
		}
	}
	check(mDevice->GetDeviceRemovedReason(), "GPU device status");
}

void Renderer::resize(UINT width, UINT height)
{
	if (!mSwapChain || !width || !height || (width == mWidth && height == mHeight))
	{
		return;
	}
	waitForGpu();
	for (auto& target : mRenderTargets) target.Reset();
	check(mSwapChain->ResizeBuffers(mBufferCount, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0), "ResizeBuffers");
	createRenderTargets();
	mWidth = width;
	mHeight = height;
}

void Renderer::render()
{
	const UINT index = mSwapChain->GetCurrentBackBufferIndex();
	check(mAllocator->Reset(), "Reset command allocator");
	check(mCommandList->Reset(mAllocator.Get(), nullptr), "Reset command list");

	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = mRenderTargets[index].Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	mCommandList->ResourceBarrier(1, &barrier);

	auto rtv = mRtvHeap->GetCPUDescriptorHandleForHeapStart();
	rtv.ptr += static_cast<SIZE_T>(index) * mRtvStride;
	constexpr float clearColor[] = {0.035f, 0.075f, 0.14f, 1.0f};
	mCommandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);

	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	mCommandList->ResourceBarrier(1, &barrier);
	check(mCommandList->Close(), "Close command list");
	ID3D12CommandList* lists[] = {mCommandList.Get()};
	mQueue->ExecuteCommandLists(1, lists);
	check(mSwapChain->Present(1, 0), "Present");
	// One frame in flight keeps allocator/resource lifetime explicit at this first milestone.
	waitForGpu();
}
