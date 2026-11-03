#pragma once

#include <windows.h>
#include <d3d12.h>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

class Renderer;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;

// Optional callbacks keep small samples independent of the window and presentation code.
// The sample object captured by callbacks must outlive runSample().
struct Sample
{
	const wchar_t* mName = L"DirectX 12 Sample";
	bool mRequireDxr = false;
	std::function<void(const std::vector<std::wstring>&)> mConfigure;
	std::function<bool()> mReadyForScreenshot;
	std::function<void(Renderer&)> mInitialize;
	// Optional graphics API entry points and shutdown for external rendering runtimes.
	std::function<HRESULT(UINT, REFIID, void**)> mCreateFactory;
	std::function<HRESULT(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**)> mCreateDevice;
	std::function<void()> mShutdown;
	// Runs after ImGui::NewFrame(), before building UI, so capture flags are current.
	std::function<void(Renderer&, float)> mUpdate;
	std::function<void()> mDrawGui;
	// The back buffer enters and must leave this callback in RENDER_TARGET state.
	std::function<void(Renderer&, ID3D12GraphicsCommandList*, ID3D12Resource*)> mRender;
};

std::filesystem::path executableDirectory();
int runSample(HINSTANCE instance, int showCommand, const Sample& sample);
