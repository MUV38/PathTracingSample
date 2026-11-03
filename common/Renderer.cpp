#include "Renderer.h"
#include "Screenshot.h"
#include "Sample.h"
#include <pix3.h>

#include <imgui.h>
#include <backends/imgui_impl_dx12.h>
#include <backends/imgui_impl_win32.h>
#include <shellapi.h>

#include <cassert>
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <utility>

namespace
{
std::filesystem::path screenshotDirectory()
{
	return executableDirectory() / L"screenshots";
}

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
	if (mSample && mSample->mShutdown)
	{
		try
		{
			mSample->mShutdown();
		}
		catch (...)
		{
			OutputDebugStringW(L"Sample shutdown failed.\n");
		}
	}
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

void Renderer::initialize(HWND window, UINT width, UINT height, const Sample& sample)
{
	PIXScopedEvent(PIX_COLOR(80, 160, 240), "Initialize DirectX 12 / ImGui");
	mSample = &sample;
	mWindow = window;
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
	const auto createFactory = sample.mCreateFactory ? sample.mCreateFactory : CreateDXGIFactory2;
	const auto createDevice = sample.mCreateDevice ? sample.mCreateDevice : D3D12CreateDevice;
	check(createFactory(factoryFlags, IID_PPV_ARGS(&mFactory)), "CreateDXGIFactory2");

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
			SUCCEEDED(createDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&mDevice))))
		{
			D3D12_FEATURE_DATA_D3D12_OPTIONS5 adapterOptions{};
			if (sample.mRequireDxr && (FAILED(mDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5,
				&adapterOptions, sizeof(adapterOptions))) || adapterOptions.RaytracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED))
			{
				mDevice.Reset();
				continue;
			}
			mAdapterName = description.Description;
			break;
		}
		adapter.Reset();
	}
	if (!mDevice)
	{
		if (sample.mRequireDxr)
		{
			throw std::runtime_error("This sample requires a DXR-capable GPU and driver (Raytracing Tier 1.0 or later).");
		}
		check(mFactory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)), "EnumWarpAdapter");
		check(createDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&mDevice)), "D3D12CreateDevice (WARP)");
		mAdapterName = L"WARP (software)";
	}

	D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};
	mRaytracingSupported = SUCCEEDED(mDevice->CheckFeatureSupport(
		D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options))) &&
		options.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;

	D3D12_COMMAND_QUEUE_DESC queueDescription{};
	queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	check(mDevice->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&mQueue)), "CreateCommandQueue");
	mQueue->SetName(L"Graphics queue");
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
	if (sample.mInitialize)
	{
		sample.mInitialize(*this);
	}
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
	PIXScopedEvent(PIX_COLOR(80, 200, 120), "Update / build ImGui");
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
	if (mSample->mUpdate)
	{
		mSample->mUpdate(*this, ImGui::GetIO().DeltaTime);
	}

	ImGui::SetNextWindowPos(ImVec2(24.0f, 24.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(300.0f, 0.0f),
		ImVec2((std::max)(300.0f, ImGui::GetIO().DisplaySize.x - 48.0f), (std::max)(200.0f, ImGui::GetIO().DisplaySize.y - 48.0f)));
	if (ImGui::Begin("Sample controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("DirectX 12 / Dear ImGui %s", IMGUI_VERSION);
		ImGui::Text("DXR: %s", mRaytracingSupported ? "supported" : "unavailable");
		ImGui::Text("Back buffer: %u x %u", mWidth, mHeight);
		const float frameRate = ImGui::GetIO().Framerate;
		ImGui::Text("%.1f FPS (%.3f ms/frame)", frameRate, frameRate > 0.0f ? 1000.0f / frameRate : 0.0f);
		ImGui::Separator();
		if (mSample->mDrawGui)
		{
			mSample->mDrawGui();
			ImGui::Separator();
		}
		ImGui::ColorEdit3("Clear color", mClearColor.data());
		if (ImGui::Button("Reset clear color"))
		{
			mClearColor = {0.035f, 0.075f, 0.14f, 1.0f};
		}
		ImGui::Checkbox("Show ImGui demo", &mShowDemoWindow);
		ImGui::Separator();
		if (ImGui::Button("Save screenshot (F12)"))
		{
			requestScreenshot();
		}
		if (ImGui::Button("Open screenshot folder"))
		{
			openScreenshotDirectory();
		}
		ImGui::TextWrapped("%s", mScreenshotStatus.empty() ? "Screenshots include the ImGui panel." : mScreenshotStatus.c_str());
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
	PIXScopedEvent(PIX_COLOR(220, 120, 80), "Wait for GPU fence");
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
	PIXScopedEvent(PIX_COLOR(70, 130, 220), "Frame / CPU");
	drawGui();
	const auto screenshotPath = std::exchange(mScreenshotPath, {});
	Screenshot screenshot;
	bool screenshotRecorded = false;
	const UINT index = mSwapChain->GetCurrentBackBufferIndex();
	check(mAllocator->Reset(), "Reset command allocator");
	check(mCommandList->Reset(mAllocator.Get(), nullptr), "Reset command list");

	{
		PIXScopedEvent(mCommandList.Get(), PIX_COLOR(70, 130, 220), "Frame / GPU");
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = mRenderTargets[index].Get();
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		mCommandList->ResourceBarrier(1, &barrier);

		auto rtv = mRtvHeap->GetCPUDescriptorHandleForHeapStart();
		rtv.ptr += static_cast<SIZE_T>(index) * mRtvStride;
		{
			PIXScopedEvent(mCommandList.Get(), PIX_COLOR(100, 100, 130), "Clear back buffer");
			mCommandList->ClearRenderTargetView(rtv, mClearColor.data(), 0, nullptr);
		}
		if (mSample->mRender)
		{
			mSample->mRender(*this, mCommandList.Get(), mRenderTargets[index].Get());
		}
		{
			PIXScopedEvent(mCommandList.Get(), PIX_COLOR(80, 200, 120), "ImGui");
			mCommandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
			ID3D12DescriptorHeap* heaps[] = {mGuiSrvHeap.Get()};
			mCommandList->SetDescriptorHeaps(1, heaps);
			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), mCommandList.Get());
		}
		if (!screenshotPath.empty())
		{
			try
			{
				screenshot.record(mDevice.Get(), mCommandList.Get(), mRenderTargets[index].Get());
				screenshotRecorded = true;
			}
			catch (const std::exception& error)
			{
				mScreenshotStatus = std::string("Screenshot failed: ") + error.what();
			}
		}

		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
		mCommandList->ResourceBarrier(1, &barrier);
	}
	check(mCommandList->Close(), "Close command list");
	ID3D12CommandList* lists[] = {mCommandList.Get()};
	{
		PIXScopedEvent(PIX_COLOR(100, 140, 220), "Submit command list");
		mQueue->ExecuteCommandLists(1, lists);
	}
	HRESULT presentResult;
	{
		PIXScopedEvent(PIX_COLOR(220, 180, 80), "Present / VSync");
		presentResult = mSwapChain->Present(1, 0);
	}
	// One frame in flight keeps allocator/resource lifetime explicit at this first milestone.
	waitForGpu();
	check(presentResult, "Present");
	if (screenshotRecorded)
	{
		try
		{
			screenshot.savePng(screenshotPath);
			mScreenshotSucceeded = true;
			const auto filename = screenshotPath.filename().u8string();
			mScreenshotStatus = "Saved: " + std::string(filename.begin(), filename.end());
		}
		catch (const std::exception& error)
		{
			mScreenshotStatus = std::string("Screenshot failed: ") + error.what();
			OutputDebugStringA(mScreenshotStatus.c_str());
		}
	}
}

void Renderer::requestScreenshot(const std::filesystem::path& path)
{
	mScreenshotSucceeded = false;
	if (!path.empty())
	{
		mScreenshotPath = path;
		return;
	}
	SYSTEMTIME time{};
	GetLocalTime(&time);
	wchar_t filename[128]{};
	swprintf_s(filename, L"screenshot-%04u%02u%02u-%02u%02u%02u-%03u-%lu.png",
		time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
		time.wMilliseconds, GetCurrentProcessId());
	mScreenshotPath = screenshotDirectory() / filename;
}

void Renderer::openScreenshotDirectory()
{
	try
	{
		const auto directory = screenshotDirectory();
		std::filesystem::create_directories(directory);
		const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		if (comResult != RPC_E_CHANGED_MODE)
		{
			check(comResult, "Initialize COM for opening the screenshot folder");
		}
		const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", directory.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
		if (SUCCEEDED(comResult))
		{
			CoUninitialize();
		}
		if (result <= 32)
		{
			throw std::runtime_error("Cannot open the screenshot folder (ShellExecute error " + std::to_string(result) + ").");
		}
		mScreenshotStatus = "Opened screenshot folder.";
	}
	catch (const std::exception& error)
	{
		mScreenshotStatus = std::string("Open folder failed: ") + error.what();
		OutputDebugStringA(mScreenshotStatus.c_str());
	}
}
