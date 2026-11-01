#include "UsdKitchen.h"
#include "Renderer.h"
#include "Sample.h"

#include <imgui.h>
#include <shobjidl.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace
{
std::string utf8(const std::filesystem::path& path)
{
	const auto bytes = path.u8string();
	return {bytes.begin(), bytes.end()};
}

std::filesystem::path fromUtf8(const char* text)
{
	std::u8string bytes;
	while (*text)
	{
		bytes.push_back(static_cast<char8_t>(*text++));
	}
	return std::filesystem::path(bytes);
}

void setPath(std::array<char, 4096>& buffer, const std::filesystem::path& path)
{
	const auto text = utf8(path);
	if (text.size() >= buffer.size())
	{
		throw std::runtime_error("The path is too long for the UI field; use command-line options instead.");
	}
	std::copy(text.begin(), text.end(), buffer.begin());
	buffer[text.size()] = '\0';
}

void check(HRESULT result, const char* operation)
{
	if (FAILED(result))
	{
		throw std::runtime_error(std::string(operation) + " failed (HRESULT " + std::to_string(static_cast<unsigned>(result)) + ").");
	}
}

std::filesystem::path chooseFile(HWND window, const wchar_t* name, const wchar_t* extensions)
{
	struct ComScope
	{
		HRESULT mResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		~ComScope()
		{
			if (SUCCEEDED(mResult))
			{
				CoUninitialize();
			}
		}
	} com;
	check(com.mResult, "Initialize file dialog");
	Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
	check(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)), "Create file dialog");
	const COMDLG_FILTERSPEC filters[] = {{name, extensions}, {L"All files", L"*.*"}};
	check(dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters), "Set file filters");
	check(dialog->SetOptions(FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR), "Set file dialog options");
	const HRESULT result = dialog->Show(window);
	if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED))
	{
		return {};
	}
	check(result, "Open file dialog");
	Microsoft::WRL::ComPtr<IShellItem> item;
	check(dialog->GetResult(&item), "Read selected file");
	PWSTR rawPath = nullptr;
	check(item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath), "Read selected path");
	const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> path(rawPath, CoTaskMemFree);
	return std::filesystem::path(path.get());
}
}

UsdKitchen::~UsdKitchen()
{
	cancelConversion();
}

void UsdKitchen::configure(const std::vector<std::wstring>& arguments)
{
	for (size_t index = 0; index < arguments.size(); ++index)
	{
		const auto& flag = arguments[index];
		if ((flag != L"--model" && flag != L"--usd" && flag != L"--converter") || index + 1 >= arguments.size())
		{
			throw std::runtime_error("UsdKitchen options: --model <scene.ptscene> OR --usd <source.usd>, optional --converter <UsdConverter.exe>.");
		}
		const std::filesystem::path path = arguments[++index];
		if (flag == L"--model")
		{
			mModelPath = path;
		}
		else if (flag == L"--usd")
		{
			mStartupUsd = path;
		}
		else
		{
			mConfiguredConverter = path;
		}
	}
	if (!mModelPath.empty() && !mStartupUsd.empty())
	{
		throw std::runtime_error("Choose either --model or --usd, not both.");
	}
}

void UsdKitchen::initialize(Renderer& renderer)
{
	mRenderer = &renderer;
	mRaytracer.initialize(renderer);
	if (mConfiguredConverter.empty())
	{
		mConfiguredConverter = executableDirectory().parent_path() / L"UsdConverter/UsdConverter.exe";
		if (!std::filesystem::exists(mConfiguredConverter))
		{
			mConfiguredConverter = executableDirectory().parent_path().parent_path() / L"Release/UsdConverter/UsdConverter.exe";
		}
	}
	setPath(mConverterInput, mConfiguredConverter);
	if (!mStartupUsd.empty())
	{
		setPath(mUsdInput, mStartupUsd);
		mStartupConversion = true;
		startConversion();
	}
	else
	{
		loadModel(mModelPath.empty() ? executableDirectory() / L"assets/PreviewRoom.ptscene" : mModelPath);
	}
}

void UsdKitchen::loadModel(const std::filesystem::path& path)
{
	auto scene = readSceneFile(path);
	mRaytracer.load(*mRenderer, scene);
	mHeader = scene.mHeader;
	mModelPath = path;
	mHasScene = true;
	mFocusPoint = {(mHeader.mMinimum[0] + mHeader.mMaximum[0]) * 0.5f,
		(mHeader.mMinimum[1] + mHeader.mMaximum[1]) * 0.5f, (mHeader.mMinimum[2] + mHeader.mMaximum[2]) * 0.5f};
	const float x = mHeader.mMaximum[0] - mHeader.mMinimum[0];
	const float y = mHeader.mMaximum[1] - mHeader.mMinimum[1];
	const float z = mHeader.mMaximum[2] - mHeader.mMinimum[2];
	mRadius = (std::max)(0.01f, std::sqrt(x * x + y * y + z * z) * 0.5f);
	frameModel();
	mNeedsFrame = true;
	mStatus = "Loaded: " + utf8(path.filename());
}

void UsdKitchen::frameModel()
{
	mCamera.reset();
	mCamera.focus(mFocusPoint);
	const float aspect = (std::max)(0.2f, (static_cast<float>(mRenderer->width()) - mViewportLeft) / mRenderer->height());
	const float limitingFov = std::atan(mCamera.tanHalfFov() * (std::min)(1.0f, aspect));
	mCamera.setOrbit(0.65f, 0.45f, mRadius / std::sin(limitingFov) * 1.08f);
	mCamera.setMoveSpeed(std::clamp(mRadius, 0.05f, 100.0f));
}

void UsdKitchen::startConversion()
{
	if (mConversionProcess)
	{
		return;
	}
	const auto source = std::filesystem::absolute(fromUtf8(mUsdInput.data()));
	const auto converter = std::filesystem::absolute(fromUtf8(mConverterInput.data()));
	if (!std::filesystem::is_regular_file(source) || !std::filesystem::is_regular_file(converter))
	{
		throw std::runtime_error("Choose an existing USD file and build/select UsdConverter.exe first.");
	}
	const auto cache = executableDirectory() / L"cache";
	std::filesystem::create_directories(cache);
	const auto stem = L"scene-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
	mPendingBinary = cache / (stem + L".ptscene");
	mLogPath = cache / (stem + L".log");
	SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
	HANDLE log = CreateFileW(mLogPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (log == INVALID_HANDLE_VALUE)
	{
		throw std::runtime_error("Cannot create the conversion log.");
	}
	HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr);
	if (input == INVALID_HANDLE_VALUE)
	{
		CloseHandle(log);
		throw std::runtime_error("Cannot initialize converter input.");
	}
	// Windows file names cannot contain quotes. Direct CreateProcess avoids shell interpretation.
	std::wstring command = L"\"" + converter.native() + L"\" \"" + source.native() + L"\" \"" + mPendingBinary.native() + L"\"";
	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdInput = input;
	startup.hStdOutput = startup.hStdError = log;
	PROCESS_INFORMATION process{};
	const BOOL created = CreateProcessW(converter.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
		nullptr, converter.parent_path().c_str(), &startup, &process);
	const DWORD error = GetLastError();
	CloseHandle(input);
	CloseHandle(log);
	if (!created)
	{
		throw std::runtime_error("Cannot start the USD converter (Win32 error " + std::to_string(error) + ").");
	}
	CloseHandle(process.hThread);
	mConversionProcess = process.hProcess;
	mLog.clear();
	mStatus = "Converting USD in a separate process...";
}

void UsdKitchen::readConversionLog()
{
	std::ifstream file(mLogPath, std::ios::binary | std::ios::ate);
	if (!file)
	{
		mLog = "Conversion log is unavailable.";
		return;
	}
	const auto length = static_cast<std::streamoff>(file.tellg());
	const auto count = (std::min)(std::streamoff(8000), length);
	file.seekg(length - count);
	mLog.resize(static_cast<size_t>(count));
	file.read(mLog.data(), count);
}

void UsdKitchen::pollConversion()
{
	if (!mConversionProcess || WaitForSingleObject(mConversionProcess, 0) != WAIT_OBJECT_0)
	{
		return;
	}
	DWORD exitCode = 1;
	GetExitCodeProcess(mConversionProcess, &exitCode);
	CloseHandle(mConversionProcess);
	mConversionProcess = nullptr;
	readConversionLog();
	try
	{
		if (exitCode != 0)
		{
			throw std::runtime_error("USD conversion failed. See Conversion log for details.\n" + mLog);
		}
		loadModel(mPendingBinary);
		mStartupConversion = false;
	}
	catch (const std::exception& error)
	{
		if (mStartupConversion)
		{
			throw;
		}
		mStatus = error.what();
	}
}

void UsdKitchen::cancelConversion()
{
	if (mConversionProcess)
	{
		if (WaitForSingleObject(mConversionProcess, 0) == WAIT_TIMEOUT)
		{
			TerminateProcess(mConversionProcess, 1);
			WaitForSingleObject(mConversionProcess, 5000);
		}
		CloseHandle(mConversionProcess);
		mConversionProcess = nullptr;
		mStatus = "Conversion canceled.";
	}
}

void UsdKitchen::update(Renderer& renderer, float deltaTime)
{
	pollConversion();
	mController.update(mCamera, renderer, deltaTime, mFocusPoint);
	if (!ImGui::GetIO().WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_F, false))
	{
		frameModel();
	}
}

void UsdKitchen::drawGui()
{
	mViewportLeft = std::clamp(ImGui::GetWindowPos().x + ImGui::GetWindowWidth() + 16.0f,
		0.0f, (std::max)(0.0f, ImGui::GetIO().DisplaySize.x - 64.0f));
	if (mNeedsFrame)
	{
		frameModel();
		mNeedsFrame = false;
	}
	ImGui::TextUnformatted("USD binary scene viewer (DXR)");
	ImGui::TextWrapped("Default: self-authored Preview Room. Kitchen Set must be obtained separately from Pixar.");
	ImGui::BeginDisabled(mConversionProcess != nullptr);
	try
	{
		if (ImGui::Button("Open USD..."))
		{
			const auto path = chooseFile(mRenderer->window(), L"USD scene", L"*.usd;*.usda;*.usdc;*.usdz");
			if (!path.empty())
			{
				setPath(mUsdInput, path);
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Open binary..."))
		{
			const auto path = chooseFile(mRenderer->window(), L"Converted scene", L"*.ptscene");
			if (!path.empty())
			{
				loadModel(path);
			}
		}
		ImGui::InputText("USD file", mUsdInput.data(), mUsdInput.size());
		ImGui::InputText("Converter", mConverterInput.data(), mConverterInput.size());
		if (ImGui::Button("Choose converter..."))
		{
			const auto path = chooseFile(mRenderer->window(), L"USD converter", L"*.exe");
			if (!path.empty())
			{
				setPath(mConverterInput, path);
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Convert and load"))
		{
			startConversion();
		}
	}
	catch (const std::exception& error)
	{
		mStatus = error.what();
	}
	ImGui::EndDisabled();
	if (mConversionProcess && ImGui::Button("Cancel conversion"))
	{
		cancelConversion();
	}
	ImGui::TextWrapped("%s", mStatus.c_str());
	if (mHasScene)
	{
		ImGui::Text("%llu triangles / %llu mesh occurrences", static_cast<unsigned long long>(mHeader.mVertexCount / 3),
			static_cast<unsigned long long>(mHeader.mMeshCount));
	}
	if (ImGui::CollapsingHeader("Conversion log"))
	{
		ImGui::TextWrapped("%s", mLog.empty() ? "No conversion in this session." : mLog.c_str());
	}
	if (ImGui::Button("Frame model (F)"))
	{
		frameModel();
	}
	ImGui::SameLine();
	ImGui::Checkbox("Headlight", &mHeadlight);
	ImGui::SliderFloat("Exposure", &mExposure, 0.1f, 3.0f);
	if (ImGui::CollapsingHeader("Camera controls"))
	{
		ImGui::TextWrapped("RMB + WASD / Q,E: move; Shift: faster; wheel: speed. Alt + LMB / MMB / RMB: orbit / pan / dolly.");
		float speed = mCamera.moveSpeed();
		if (ImGui::SliderFloat("Move speed", &speed, 0.05f, 100.0f, "%.2f units/s", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp))
		{
			mCamera.setMoveSpeed(speed);
		}
		const auto position = mCamera.position();
		ImGui::Text("Position: %.2f, %.2f, %.2f", position.x, position.y, position.z);
	}
}

void UsdKitchen::render(Renderer& renderer, ID3D12GraphicsCommandList* commands, ID3D12Resource* backBuffer)
{
	if (mHasScene)
	{
		mRaytracer.render(renderer, mCamera, mViewportLeft, mHeadlight, mExposure, commands, backBuffer);
	}
}
