#include "Renderer.h"

#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <shellapi.h>

#include <exception>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string_view>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

// These exports opt the executable into the app-local Agility SDK runtime.
extern "C"
{
	__declspec(dllexport) extern const UINT D3D12SDKVersion = 722;
	__declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\";
}

namespace
{
struct WindowState
{
	UINT mWidth = 1280;
	UINT mHeight = 720;
	bool mMinimized = false;
	bool mScreenshotRequested = false;
};

LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam))
	{
		return 1;
	}
	auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
	if (message == WM_NCCREATE)
	{
		state = static_cast<WindowState*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
		SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
	}
	switch (message)
	{
	case WM_SIZE:
		if (state)
		{
			state->mWidth = LOWORD(lParam);
			state->mHeight = HIWORD(lParam);
			state->mMinimized = wParam == SIZE_MINIMIZED;
		}
		return 0;
	case WM_ERASEBKGND:
		return 1;
	case WM_KEYDOWN:
		if (wParam == VK_F12)
		{
			if (state && (lParam & (1LL << 30)) == 0)
			{
				state->mScreenshotRequested = true;
			}
			return 0;
		}
		if (wParam == VK_ESCAPE && (!ImGui::GetCurrentContext() || !ImGui::GetIO().WantCaptureKeyboard))
		{
			DestroyWindow(window);
			return 0;
		}
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(window, message, wParam, lParam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
	HWND window = nullptr;
	WindowState state;
	bool screenshotMode = false;
	try
	{
		int argumentCount = 0;
		const std::unique_ptr<wchar_t*, decltype(&LocalFree)> arguments(
			CommandLineToArgvW(GetCommandLineW(), &argumentCount), LocalFree);
		if (!arguments)
		{
			throw std::runtime_error("Cannot parse command line arguments.");
		}
		screenshotMode = argumentCount > 1;
		std::filesystem::path screenshotPath;
		if (screenshotMode)
		{
			if (argumentCount != 3 || std::wstring_view(arguments.get()[1]) != L"--screenshot" || arguments.get()[2][0] == L'\0')
			{
				throw std::runtime_error("Usage: PathTracingSample.exe [--screenshot <output.png>]");
			}
			screenshotPath = arguments.get()[2];
		}
		SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
		constexpr wchar_t ClassName[] = L"PathTracingSampleWindow";
		WNDCLASSEXW windowClass{};
		windowClass.cbSize = sizeof(windowClass);
		windowClass.style = CS_HREDRAW | CS_VREDRAW;
		windowClass.lpfnWndProc = windowProcedure;
		windowClass.hInstance = instance;
		windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		windowClass.lpszClassName = ClassName;
		if (!RegisterClassExW(&windowClass))
		{
			throw std::runtime_error("RegisterClassExW failed.");
		}

		RECT rectangle{0, 0, static_cast<LONG>(state.mWidth), static_cast<LONG>(state.mHeight)};
		if (!AdjustWindowRectEx(&rectangle, WS_OVERLAPPEDWINDOW, FALSE, 0))
		{
			throw std::runtime_error("AdjustWindowRectEx failed.");
		}
		window = CreateWindowExW(0, ClassName, L"PathTracingSample", WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT, CW_USEDEFAULT, rectangle.right - rectangle.left,
			rectangle.bottom - rectangle.top, nullptr, nullptr, instance, &state);
		if (!window)
		{
			throw std::runtime_error("CreateWindowExW failed.");
		}

		Renderer renderer;
		renderer.initialize(window, state.mWidth, state.mHeight);
		const std::wstring title = L"PathTracingSample | DirectX 12 | " + renderer.adapterName() +
			(renderer.supportsRaytracing() ? L" | DXR supported" : L" | DXR unavailable");
		SetWindowTextW(window, title.c_str());
		ShowWindow(window, showCommand);

		MSG message{};
		UINT renderedFrames = 0;
		while (message.message != WM_QUIT)
		{
			if (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&message);
				DispatchMessageW(&message);
			}
			else if (state.mMinimized)
			{
				WaitMessage();
			}
			else
			{
				renderer.resize(state.mWidth, state.mHeight);
				const bool captureAndExit = screenshotMode && renderedFrames >= 2;
				if (captureAndExit || state.mScreenshotRequested)
				{
					renderer.requestScreenshot(captureAndExit ? screenshotPath : std::filesystem::path{});
					state.mScreenshotRequested = false;
				}
				renderer.render();
				++renderedFrames;
				if (captureAndExit)
				{
					const int result = renderer.screenshotSucceeded() ? 0 : 1;
					if (result != 0)
					{
						std::fprintf(stderr, "%s\n", renderer.screenshotStatus().c_str());
					}
					DestroyWindow(window);
					return result;
				}
			}
		}
		renderer.waitForGpu();
		return static_cast<int>(message.wParam);
	}
	catch (const std::exception& error)
	{
		OutputDebugStringA(error.what());
		std::fprintf(stderr, "%s\n", error.what());
		if (!screenshotMode)
		{
			MessageBoxA(window, error.what(), "PathTracingSample - Error", MB_OK | MB_ICONERROR);
		}
		if (window && IsWindow(window))
		{
			DestroyWindow(window);
		}
		return 1;
	}
}
