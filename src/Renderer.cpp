#include "Renderer.h"

#include <imgui.h>
#include <backends/imgui_impl_dx12.h>
#include <backends/imgui_impl_win32.h>

#include <cassert>
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
	if (mGuiDx12Initialized)
	{
		ImGui_ImplDX12_Shutdown();
	}
	if (mGuiWin32Initialized)
	{
		ImGui_ImplWin32_Shutdown();
	}
	if (mGuiContextCreated)
	{
		ImGui::DestroyContext();
	}
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
	initializeGui(window);
}

void Renderer::initializeGui(HWND window)
{
	D3D12_DESCRIPTOR_HEAP_DESC heapDescription{};
	heapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDescription.NumDescriptors = mGuiDescriptorCount;
	heapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	check(mDevice->CreateDescriptorHeap(&heapDescription, IID_PPV_ARGS(&mGuiSrvHeap)), "Create ImGui SRV heap");
	mGuiDescriptorStride = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	mGuiContextCreated = true;
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.IniFilename = nullptr;
	ImGui::StyleColorsDark();
	const float dpiScale = ImGui_ImplWin32_GetDpiScaleForHwnd(window);
	ImGui::GetStyle().ScaleAllSizes(dpiScale);
	ImGui::GetStyle().FontScaleDpi = dpiScale;
	if (!ImGui_ImplWin32_Init(window))
	{
		throw std::runtime_error("ImGui Win32 initialization failed.");
	}
	mGuiWin32Initialized = true;

	ImGui_ImplDX12_InitInfo info{};
	info.Device = mDevice.Get();
	info.CommandQueue = mQueue.Get();
	info.NumFramesInFlight = mBufferCount;
	info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	info.DSVFormat = DXGI_FORMAT_UNKNOWN;
	info.UserData = this;
	info.SrvDescriptorHeap = mGuiSrvHeap.Get();
	info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* initInfo,
		D3D12_CPU_DESCRIPTOR_HANDLE* cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* gpuHandle)
	{
		static_cast<Renderer*>(initInfo->UserData)->allocateGuiDescriptor(cpuHandle, gpuHandle);
	};
	info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* initInfo,
		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE)
	{
		static_cast<Renderer*>(initInfo->UserData)->freeGuiDescriptor(cpuHandle);
	};
	if (!ImGui_ImplDX12_Init(&info))
	{
		throw std::runtime_error("ImGui DirectX 12 initialization failed.");
	}
	mGuiDx12Initialized = true;
}

void Renderer::allocateGuiDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE* cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* gpuHandle)
{
	for (UINT index = 0; index < mGuiDescriptorCount; ++index)
	{
		if (!mGuiDescriptorsInUse[index])
		{
			mGuiDescriptorsInUse[index] = true;
			*cpuHandle = mGuiSrvHeap->GetCPUDescriptorHandleForHeapStart();
			*gpuHandle = mGuiSrvHeap->GetGPUDescriptorHandleForHeapStart();
			cpuHandle->ptr += static_cast<SIZE_T>(index) * mGuiDescriptorStride;
			gpuHandle->ptr += static_cast<UINT64>(index) * mGuiDescriptorStride;
			return;
		}
	}
	throw std::runtime_error("ImGui SRV descriptor heap is full.");
}

void Renderer::freeGuiDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle)
{
	const SIZE_T offset = cpuHandle.ptr - mGuiSrvHeap->GetCPUDescriptorHandleForHeapStart().ptr;
	const SIZE_T index = offset / mGuiDescriptorStride;
	assert(offset % mGuiDescriptorStride == 0 && index < mGuiDescriptorCount);
	assert(mGuiDescriptorsInUse[index]);
	mGuiDescriptorsInUse[index] = false;
}

void Renderer::drawGui()
{
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	ImGui::SetNextWindowPos(ImVec2(24.0f, 24.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("PathTracingSample", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("DirectX 12 / Dear ImGui %s", IMGUI_VERSION);
		ImGui::Text("DXR: %s", mRaytracingSupported ? "supported" : "unavailable");
		ImGui::Text("Back buffer: %u x %u", mWidth, mHeight);
		const float frameRate = ImGui::GetIO().Framerate;
		ImGui::Text("%.1f FPS (%.3f ms/frame)", frameRate, frameRate > 0.0f ? 1000.0f / frameRate : 0.0f);
		ImGui::Separator();
		ImGui::ColorEdit3("Clear color", mClearColor.data());
		if (ImGui::Button("Reset clear color"))
		{
			mClearColor = {0.035f, 0.075f, 0.14f, 1.0f};
		}
		ImGui::Checkbox("Show ImGui demo", &mShowDemoWindow);
	}
	ImGui::End();
	if (mShowDemoWindow)
	{
		ImGui::ShowDemoWindow(&mShowDemoWindow);
	}
	ImGui::Render();
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
	drawGui();
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
	mCommandList->ClearRenderTargetView(rtv, mClearColor.data(), 0, nullptr);
	mCommandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
	ID3D12DescriptorHeap* heaps[] = {mGuiSrvHeap.Get()};
	mCommandList->SetDescriptorHeaps(1, heaps);
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), mCommandList.Get());

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
