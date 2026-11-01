#pragma once

#include <windows.h>

#include <filesystem>
#include <functional>

class Renderer;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;

// Optional callbacks keep small samples independent of the window and presentation code.
// The sample object captured by callbacks must outlive runSample().
struct Sample
{
	const wchar_t* mName = L"DirectX 12 Sample";
	bool mRequireDxr = false;
	std::function<void(Renderer&)> mInitialize;
	// Runs after ImGui::NewFrame(), before building UI, so capture flags are current.
	std::function<void(Renderer&, float)> mUpdate;
	std::function<void()> mDrawGui;
	// The back buffer enters and must leave this callback in RENDER_TARGET state.
	std::function<void(Renderer&, ID3D12GraphicsCommandList*, ID3D12Resource*)> mRender;
};

std::filesystem::path executableDirectory();
int runSample(HINSTANCE instance, int showCommand, const Sample& sample);
