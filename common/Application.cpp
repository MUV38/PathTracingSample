#include "Renderer.h"
#include "Sample.h"

#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <shellapi.h>
#include <windowsx.h>

#include <exception>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>

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
	bool mRawMouseRegistered = false;
	MouseMotion mMouseMotion;
};

LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
	if ((message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) && ImGui::GetCurrentContext())
	{
		// Wheel messages carry their own cursor position; enqueue it before the wheel event
		// so moving from the viewport onto UI and immediately scrolling cannot use stale hover state.
		POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
		if (ScreenToClient(window, &point))
		{
			ImGui::GetIO().AddMousePosEvent(static_cast<float>(point.x), static_cast<float>(point.y));
		}
	}
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
	case WM_INPUT:
		if (state)
		{
			RAWINPUT input{};
			UINT size = sizeof(input);
			if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER)) != UINT(-1)
				&& input.header.dwType == RIM_TYPEMOUSE)
			{
				if ((input.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0)
				{
					state->mMouseMotion.mDeltaX += static_cast<float>(input.data.mouse.lLastX);
					state->mMouseMotion.mDeltaY += static_cast<float>(input.data.mouse.lLastY);
				}
				else
				{
					state->mMouseMotion.mUseRawMouse = false;
				}
			}
		}
		// DefWindowProc releases the foreground raw-input packet.
		break;
	case WM_KILLFOCUS:
		if (state)
		{
			state->mMouseMotion = {};
		}
		break;
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
	case WM_SYSCOMMAND:
		if ((wParam & 0xfff0) == SC_KEYMENU && lParam == 0)
		{
			return 0; // Bare Alt belongs to viewport camera gestures; keep Alt+Space available.
		}
		break;
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

std::filesystem::path executableDirectory()
{
	std::array<wchar_t, 32768> path{};
	const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
	if (length == 0 || length >= path.size())
	{
		throw std::runtime_error("Cannot locate the executable directory.");
	}
	return std::filesystem::path(path.data()).parent_path();
}

int runSample(HINSTANCE instance, int showCommand, const Sample& sample)
{
	HWND window = nullptr;
	WindowState state;
	bool screenshotMode = false;
	bool commandLineMode = false;
	try
	{
		int argumentCount = 0;
		const std::unique_ptr<wchar_t*, decltype(&LocalFree)> arguments(
			CommandLineToArgvW(GetCommandLineW(), &argumentCount), LocalFree);
		if (!arguments)
		{
			throw std::runtime_error("Cannot parse command line arguments.");
		}
		commandLineMode = argumentCount > 1;
		std::filesystem::path screenshotPath;
		std::vector<std::wstring> sampleArguments;
		for (int index = 1; index < argumentCount; ++index)
		{
			if (std::wstring_view(arguments.get()[index]) == L"--screenshot")
			{
				if (screenshotMode || ++index >= argumentCount || arguments.get()[index][0] == L'\0')
				{
					throw std::runtime_error("Usage: <sample.exe> [--screenshot <output.png>] [sample options]");
				}
				screenshotMode = true;
				screenshotPath = arguments.get()[index];
			}
			else
			{
				sampleArguments.emplace_back(arguments.get()[index]);
			}
		}
		if (sample.mConfigure)
		{
			sample.mConfigure(sampleArguments);
		}
		else if (!sampleArguments.empty())
		{
			throw std::runtime_error("This sample only accepts --screenshot <output.png>.");
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
		window = CreateWindowExW(0, ClassName, sample.mName, WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT, CW_USEDEFAULT, rectangle.right - rectangle.left,
			rectangle.bottom - rectangle.top, nullptr, nullptr, instance, &state);
		if (!window)
		{
			throw std::runtime_error("CreateWindowExW failed.");
		}
		RAWINPUTDEVICE mouse{0x01, 0x02, 0, window};
		state.mRawMouseRegistered = RegisterRawInputDevices(&mouse, 1, sizeof(mouse)) != FALSE;
		state.mMouseMotion.mUseRawMouse = state.mRawMouseRegistered;

		Renderer renderer;
		renderer.initialize(window, state.mWidth, state.mHeight, sample);
		const std::wstring title = std::wstring(sample.mName) + L" | DirectX 12 | " + renderer.adapterName() +
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
				state.mMouseMotion = {0.0f, 0.0f, state.mRawMouseRegistered};
				WaitMessage();
			}
			else
			{
				renderer.resize(state.mWidth, state.mHeight);
				renderer.setMouseMotion(std::exchange(state.mMouseMotion, MouseMotion{0.0f, 0.0f, state.mRawMouseRegistered}));
				const bool captureAndExit = screenshotMode && renderedFrames >= 2
					&& (!sample.mReadyForScreenshot || sample.mReadyForScreenshot());
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
		if (!commandLineMode)
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
