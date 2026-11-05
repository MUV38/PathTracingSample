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
std::filesystem::path assetDirectory()
{
	const auto executable = executableDirectory();
	for (auto directory = executable; !directory.empty();)
	{
		if (std::filesystem::is_regular_file(directory / L"PathTracingSample.slnx"))
		{
			return directory / L"assets";
		}
		const auto parent = directory.parent_path();
		if (parent == directory)
		{
			break;
		}
		directory = parent;
	}
	return executable / L"assets";
}

std::filesystem::path defaultScenePath()
{
	const auto assets = assetDirectory();
	for (const auto* name : {L"Kitchen_set.ptscene", L"Kitchen_set_instanced.ptscene"})
	{
		const auto kitchen = assets / L"local" / name;
		if (std::filesystem::is_regular_file(kitchen))
		{
			return kitchen;
		}
	}
	return executableDirectory() / L"assets/PreviewRoom.ptscene";
}

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
		if (flag == L"--gi-validation" && index + 1 < arguments.size())
		{
			const std::filesystem::path path = arguments[++index];
			if (!path.parent_path().empty())
			{
				std::filesystem::create_directories(path.parent_path());
			}
			mGiValidationLog.open(path);
			if (!mGiValidationLog)
			{
				throw std::runtime_error("Cannot create GI validation CSV.");
			}
			mGiValidationLog << "frame,event,history,width,height\n";
			continue;
		}
		if (flag == L"--rr-view" && index + 1 < arguments.size())
		{
			const auto& value = arguments[++index];
			const std::array<std::wstring, 9> views = {L"output", L"noisy", L"normal", L"roughness", L"diffuse", L"specular", L"motion", L"depth", L"hit-distance"};
			const auto view = std::find(views.begin(), views.end(), value);
			if (view == views.end())
			{
				throw std::runtime_error("--rr-view expects output, noisy, normal, roughness, diffuse, specular, motion, depth, or hit-distance.");
			}
			mRaytracer.setRayReconstructionView(static_cast<uint32_t>(view - views.begin()));
			mRaytracer.setRayReconstructionEnabled(true);
			continue;
		}
		if (flag == L"--rr" || flag == L"--no-rr")
		{
			mRaytracer.setRayReconstructionEnabled(flag == L"--rr");
			continue;
		}
		if (flag == L"--add-rect-light")
		{
			if (mLighting.mRectangleCount == Lighting::maximumRectangles)
			{
				throw std::runtime_error("At most 32 rectangle lights are supported.");
			}
			mSelectedRectangle = static_cast<int>(mLighting.mRectangleCount++);
			mLighting.mRectangles[mSelectedRectangle].mEnabled = true;
			continue;
		}
		if (flag == L"--rect-index" && index + 1 < arguments.size())
		{
			const auto& value = arguments[++index];
			size_t consumed = 0;
			const auto selected = std::stoul(value, &consumed);
			if (consumed != value.size() || selected >= mLighting.mRectangleCount)
			{
				throw std::runtime_error("--rect-index must name an existing zero-based rectangle slot.");
			}
			mSelectedRectangle = static_cast<int>(selected);
			continue;
		}
		if (flag == L"--rect-light" || flag == L"--no-rect-light" || flag == L"--no-rect-mis")
		{
			if (flag == L"--rect-light")
			{
				mLighting.mRectangles[mSelectedRectangle].mEnabled = true;
			}
			else if (flag == L"--no-rect-light")
			{
				mLighting.mRectangles[mSelectedRectangle].mEnabled = false;
			}
			else
			{
				mLighting.mRectangleMis = false;
			}
			continue;
		}
		if (flag == L"--rect-position" || flag == L"--rect-size" || flag == L"--rect-orientation"
			|| flag == L"--rect-color" || flag == L"--rect-radiance")
		{
			const size_t count = flag == L"--rect-radiance" ? 1 : (flag == L"--rect-size" || flag == L"--rect-orientation" ? 2 : 3);
			if (index + count >= arguments.size())
			{
				throw std::runtime_error("Rectangle options: --rect-position x y z, --rect-size width height, --rect-orientation azimuth elevation, --rect-color r g b, --rect-radiance value.");
			}
			std::array<float,3> values{};
			for (size_t axis = 0; axis < count; ++axis)
			{
				const auto& value = arguments[++index];
				size_t consumed = 0;
				values[axis] = std::stof(value, &consumed);
				float minimum = -100000, maximum = 100000;
				if (flag == L"--rect-size")
				{
					minimum = 0.01f;
					maximum = 100;
				}
				else if (flag == L"--rect-color" || flag == L"--rect-radiance")
				{
					minimum = 0;
					maximum = flag == L"--rect-color" ? 1.0f : 10000.0f;
				}
				else if (flag == L"--rect-orientation")
				{
					maximum = axis == 0 ? 180.0f : 90.0f;
					minimum = -maximum;
				}
				if (consumed != value.size() || !std::isfinite(values[axis]) || values[axis] < minimum || values[axis] > maximum)
				{
					throw std::runtime_error("Rectangle light parameter is outside its valid range.");
				}
			}
			auto& rectangle = mLighting.mRectangles[mSelectedRectangle];
			if (flag == L"--rect-position")
			{
				rectangle.mPosition = {values[0], values[1], values[2]};
			}
			else if (flag == L"--rect-size")
			{
				rectangle.mWidth = values[0];
				rectangle.mHeight = values[1];
			}
			else if (flag == L"--rect-orientation")
			{
				rectangle.mAzimuth = values[0];
				rectangle.mElevation = values[1];
			}
			else if (flag == L"--rect-color")
			{
				rectangle.mColor = values;
			}
			else
			{
				rectangle.mRadiance = values[0];
			}
			continue;
		}
		if (flag == L"--meshlet-colors")
		{
			mMeshletColors = true;
			continue;
		}
		if (flag == L"--no-shadows")
		{
			mLighting.mShadows = false;
			continue;
		}
		if ((flag == L"--metallic" || flag == L"--roughness" || flag == L"--specular") && index + 1 < arguments.size())
		{
			const auto& value = arguments[++index];
			size_t consumed = 0;
			const float number = std::stof(value, &consumed);
			if (consumed != value.size() || !std::isfinite(number) || number < 0 || number > 1)
			{
				throw std::runtime_error("Metallic, roughness and specular must be 0..1.");
			}
			if (flag == L"--metallic")
			{
				mOverrideMetallic = number;
			}
			else if (flag == L"--roughness")
			{
				mOverrideRoughness = number;
			}
			else
			{
				mOverrideSpecular = number;
			}
			continue;
		}
		if ((flag == L"--bounces" || flag == L"--spp" || flag == L"--samples") && index + 1 < arguments.size())
		{
			const auto& value = arguments[++index];
			size_t consumed = 0;
			const auto number = std::stoul(value, &consumed);
			const uint32_t minimum = flag == L"--bounces" ? 0 : 1;
			const uint32_t maximum = flag == L"--bounces" ? 8 : flag == L"--spp" ? 16 : 65536;
			if (consumed != value.size() || number < minimum || number > maximum)
			{
				throw std::runtime_error("Bounces must be 0..8, spp 1..16, screenshot samples 1..65536.");
			}
			if (flag == L"--bounces")
			{
				mTracing.mMaxBounces = static_cast<uint32_t>(number);
			}
			else if (flag == L"--spp")
			{
				mTracing.mSamplesPerFrame = static_cast<uint32_t>(number);
			}
			else
			{
				mScreenshotSamples = number;
			}
			continue;
		}
		if (flag == L"--no-emissive-mis")
		{
			mTracing.mEmissiveMis = false;
			continue;
		}
		if (flag == L"--no-ibl-mis")
		{
			mTracing.mEnvironmentMis = false;
			continue;
		}
		if (flag == L"--no-multiscattering")
		{
			mTracing.mMultipleScattering = false;
			continue;
		}
		if (flag == L"--no-russian-roulette")
		{
			mTracing.mRussianRoulette = false;
			continue;
		}
		if (flag == L"--restir-gi" && index + 1 < arguments.size())
		{
			const auto& value = arguments[++index];
			if (value != L"off" && value != L"candidates" && value != L"temporal" && value != L"spatial")
			{
				throw std::runtime_error("--restir-gi expects off, candidates, temporal, or spatial.");
			}
			mTracing.mGiMode = value == L"spatial" ? 3u : value == L"temporal" ? 2u : (value == L"candidates" ? 1u : 0u);
			continue;
		}
		if (flag == L"--restir-pt" && index + 1 < arguments.size())
		{
			const auto& value = arguments[++index];
			if (value != L"temporal" && value != L"hybrid" && value != L"reservoir" && value != L"spatial" && value != L"spatiotemporal" && value != L"off")
			{
				throw std::runtime_error("--restir-pt expects off, temporal, hybrid, reservoir, spatial, or spatiotemporal.");
			}
			mTracing.mGiMode = value == L"spatiotemporal" ? 8u : value == L"spatial" ? 7u : value == L"reservoir" ? 6u
				: value == L"hybrid" ? 5u : value == L"temporal" ? 4u : 0u;
			continue;
		}
		if (flag == L"--path-replay" || flag == L"--path-replay-check")
		{
			mTracing.mPathReplay = flag == L"--path-replay-check" ? 2u : 1u;
			continue;
		}
		if (flag == L"--pt-refresh-period")
		{
			if (index + 1 >= arguments.size())
			{
				throw std::runtime_error("--pt-refresh-period expects 0..256.");
			}
			const auto& value = arguments[++index];
			size_t consumed = 0;
			const auto period = std::stoull(value, &consumed);
			if (consumed != value.size() || period > 256 || value.find_first_not_of(L"0123456789") != std::wstring::npos)
			{
				throw std::runtime_error("--pt-refresh-period expects 0..256.");
			}
			mTracing.mPtRefreshPeriod = static_cast<uint32_t>(period);
			continue;
		}
		if (flag == L"--seed")
		{
			if (index + 1 >= arguments.size())
			{
				throw std::runtime_error("--seed expects an unsigned 32-bit integer.");
			}
			const auto& value = arguments[++index];
			if (value.empty() || value.find_first_not_of(L"0123456789") != std::wstring::npos)
			{
				throw std::runtime_error("--seed expects an unsigned 32-bit integer.");
			}
			const auto seed = std::stoull(value);
			if (seed > UINT32_MAX)
			{
				throw std::runtime_error("--seed expects an unsigned 32-bit integer.");
			}
			mTracing.mSeed = static_cast<uint32_t>(seed);
			continue;
		}
		if (flag == L"--pt-reservoir-confidence" && index + 1 < arguments.size())
		{
			const auto& value = arguments[++index];
			size_t consumed = 0;
			const auto number = std::stoul(value, &consumed);
			if (consumed != value.size() || number < 2 || number > 8)
			{
				throw std::runtime_error("--pt-reservoir-confidence expects 2..8.");
			}
			mTracing.mPtReservoirConfidence = static_cast<uint32_t>(number);
			continue;
		}
		if (flag == L"--hybrid-unoptimized")
		{
			mTracing.mHybridUnoptimized = true;
			continue;
		}
		if (flag == L"--no-gi-spatial")
		{
			mTracing.mGiSpatial = false;
			continue;
		}
		if (flag == L"--no-gi-history")
		{
			mTracing.mGiHistory = false;
			continue;
		}
		if (flag == L"--gi-view" && index + 1 < arguments.size())
		{
			const auto& value = arguments[++index];
			const std::array<std::wstring,14> views{L"output", L"diffuse", L"remainder", L"radiance", L"normal", L"pdf", L"valid", L"error", L"history", L"diffuse-noisy", L"spatial", L"hybrid-cost", L"hybrid-quality", L"reservoir" };
			const auto view = std::find(views.begin(), views.end(), value);
			if (view == views.end())
			{
				throw std::runtime_error("--gi-view expects output, diffuse, remainder, radiance, normal, pdf, valid, error, history, diffuse-noisy, spatial, hybrid-cost, hybrid-quality, or reservoir.");
			}
			mTracing.mGiView = static_cast<uint32_t>(view - views.begin());
			continue;
		}
		if ((flag == L"--restir" || flag == L"--restir-debug") && index + 1 < arguments.size())
		{
			const auto& value = arguments[++index];
			if (flag == L"--restir")
			{
				if (value == L"off")
				{
					mTracing.mRestirMode = 0;
				}
				else if (value == L"ris")
				{
					mTracing.mRestirMode = 1;
				}
				else if (value == L"temporal")
				{
					mTracing.mRestirMode = 2;
				}
				else if (value == L"spatial")
				{
					mTracing.mRestirMode = 3;
				}
				else
				{
					throw std::runtime_error("--restir expects off, ris, temporal, or spatial.");
				}
			}
			else
			{
				if (value == L"off")
				{
					mTracing.mRestirDebug = 0;
				}
				else if (value == L"normal")
				{
					mTracing.mRestirDebug = 1;
				}
				else if (value == L"depth")
				{
					mTracing.mRestirDebug = 2;
				}
				else if (value == L"motion")
				{
					mTracing.mRestirDebug = 3;
				}
				else if (value == L"reservoir")
				{
					mTracing.mRestirDebug = 4;
				}
				else
				{
					throw std::runtime_error("--restir-debug expects off, normal, depth, motion, or reservoir.");
				}
			}
			continue;
		}
		if (flag == L"--gpu-timing" && index + 1 < arguments.size())
		{
			mGpuTimingPath = arguments[++index];
			continue;
		}
		if (flag == L"--gpu-budget-ms" && index + 1 < arguments.size())
		{
			const auto& value = arguments[++index];
			size_t consumed = 0;
			mScreenshotGpuBudget = std::stod(value, &consumed);
			if (consumed != value.size() || !std::isfinite(mScreenshotGpuBudget) || mScreenshotGpuBudget < 100 || mScreenshotGpuBudget > 5000)
			{
				throw std::runtime_error("--gpu-budget-ms must be 100..5000 milliseconds.");
			}
			continue;
		}
		if ((flag == L"--ibl-intensity" || flag == L"--ibl-rotation") && index + 1 < arguments.size())
		{
			const auto& value = arguments[++index];
			size_t consumed = 0;
			const float number = std::stof(value, &consumed);
			const float minimum = flag == L"--ibl-rotation" ? -180.0f : 0.0f;
			const float maximum = flag == L"--ibl-rotation" ? 180.0f : 100.0f;
			if (consumed != value.size() || !std::isfinite(number) || number < minimum || number > maximum)
			{
				throw std::runtime_error("IBL intensity must be 0..100; rotation must be -180..180 degrees.");
			}
			if (flag == L"--ibl-rotation")
			{
				mTracing.mEnvironmentRotation = number;
			}
			else
			{
				mTracing.mEnvironmentIntensity = number;
			}
			continue;
		}
		if ((flag == L"--light" || flag == L"--ambient" || flag == L"--light-strength") && index + 1 < arguments.size())
		{
			const auto& value = arguments[++index];
			if (flag == L"--light")
			{
				if (value != L"directional" && value != L"point")
				{
					throw std::runtime_error("--light must be directional or point.");
				}
				mLighting.mPointLight = value == L"point";
			}
			else
			{
				size_t consumed = 0;
				const float number = std::stof(value, &consumed);
				const float maximum = flag == L"--ambient" ? 2.0f : 200.0f;
				if (consumed != value.size() || !std::isfinite(number) || number < 0.0f || number > maximum)
				{
					throw std::runtime_error("Light strength must be in [0, 200], ambient in [0, 2].");
				}
				if (flag == L"--ambient")
				{
					mLighting.mAmbient = number;
				}
				else
				{
					mLighting.mDirectionalStrength = mLighting.mPointStrength = number;
				}
			}
			continue;
		}
		if ((flag != L"--model" && flag != L"--usd" && flag != L"--converter" && flag != L"--ibl" && flag != L"--conversion-output") || index + 1 >= arguments.size())
		{
			throw std::runtime_error("UsdKitchen options: --rr / --no-rr, --rr-view <output|noisy|normal|roughness|diffuse|specular|motion|depth|hit-distance>, --model / --usd <file>, --converter <exe>, --conversion-output <file.ptscene>, --ibl <file.hdr>, --bounces <0..8>, --spp <1..16>, --samples <1..65536>, --ibl-intensity <0..100>, --ibl-rotation <-180..180>, --gpu-timing <file.csv>, --gpu-budget-ms <100..5000>, --no-ibl-mis, --no-emissive-mis, --no-multiscattering, --no-russian-roulette, --restir <off|ris|temporal|spatial>, --restir-debug <off|normal|depth|motion|reservoir>, --rect-light / --no-rect-light, --add-rect-light, --rect-index <slot>, --rect-position x y z, --rect-size width height, --rect-orientation azimuth elevation, --rect-color r g b, --rect-radiance <0..10000>, --no-rect-mis, --metallic / --roughness / --specular <0..1>, --meshlet-colors, --light <directional|point>, --no-shadows, --ambient <0..2>, --light-strength <0..200>.");
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
		else if (flag == L"--ibl")
		{
			mEnvironmentPath = path;
		}
		else if (flag == L"--conversion-output")
		{
			mConfiguredOutput = path;
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
	mRaytracer.rayReconstruction().start();
}

void UsdKitchen::initialize(Renderer& renderer)
{
	mRenderer = &renderer;
	mRaytracer.initialize(renderer);
	if (mRaytracer.rayReconstructionEnabled() && !mRaytracer.rayReconstruction().supported())
	{
		throw std::runtime_error(mRaytracer.rayReconstruction().status());
	}
	if (!mGpuTimingPath.empty())
	{
		if (!mGpuTimingPath.parent_path().empty())
		{
			std::filesystem::create_directories(mGpuTimingPath.parent_path());
		}
		mGpuTimingLog.open(mGpuTimingPath);
		if (!mGpuTimingLog)
		{
			throw std::runtime_error("Cannot write --gpu-timing output.");
		}
		mGpuTimingLog << "gpu_ms,samples\n";
	}
	mRaytracer.loadEnvironment(renderer, mEnvironmentPath.empty() ? createPreviewEnvironment() : readRadianceEnvironment(mEnvironmentPath));
	if (!mEnvironmentPath.empty())
	{
		mEnvironmentName = utf8(mEnvironmentPath.filename());
	}
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
		loadModel(mModelPath.empty() ? defaultScenePath() : mModelPath);
	}
}

void UsdKitchen::loadModel(const std::filesystem::path& path)
{
	auto scene = readSceneFile(path);
	for (auto& material : scene.mMaterials)
	{
		if (mOverrideMetallic)
		{
			material.mMetallic = *mOverrideMetallic;
		}
		if (mOverrideRoughness)
		{
			material.mRoughness = *mOverrideRoughness;
		}
		if (mOverrideSpecular)
		{
			material.mSpecular = *mOverrideSpecular;
		}
	}
	mRaytracer.load(*mRenderer, scene);
	mMaterials = scene.mMaterials;
	mSelectedMaterial = 0;
	mHeader = scene.mHeader;
	mTriangleCount = scene.mMeshletTriangles.size() / 3;
	mMeshletCount = scene.mMeshlets.size();
	mModelPath = path;
	mHasScene = true;
	mFocusPoint = {(mHeader.mMinimum[0] + mHeader.mMaximum[0]) * 0.5f,
		(mHeader.mMinimum[1] + mHeader.mMaximum[1]) * 0.5f, (mHeader.mMinimum[2] + mHeader.mMaximum[2]) * 0.5f};
	const float x = mHeader.mMaximum[0] - mHeader.mMinimum[0];
	const float y = mHeader.mMaximum[1] - mHeader.mMinimum[1];
	const float z = mHeader.mMaximum[2] - mHeader.mMinimum[2];
	mRadius = (std::max)(0.01f, std::sqrt(x * x + y * y + z * z) * 0.5f);
	mLighting.mPosition = {mFocusPoint.x + x * 0.2f, mHeader.mMinimum[1] + y * 0.8f, mFocusPoint.z + z * 0.2f};
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
	mPendingBinary = mConfiguredOutput.empty() ? assetDirectory() / L"local" / (source.stem().native() + L".ptscene")
		: std::filesystem::absolute(mConfiguredOutput);
	std::filesystem::create_directories(mPendingBinary.parent_path());
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
	if (mGiValidationLog.is_open() && mHasScene)
	{
		updateGiValidation(renderer);
	}
	if (!ImGui::GetIO().WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_F, false))
	{
		frameModel();
	}
}

void UsdKitchen::updateGiValidation(Renderer& renderer)
{
	// Explicit CLI-only integration test sequence. Ordinary runs never enter here.
	mGiValidationEvent = "none";
	if (mGiValidationFrame >= 12 && mGiValidationFrame < 20)
	{
		auto pivot = mFocusPoint;
		pivot.x += mRadius * 0.001f * float(mGiValidationFrame - 11);
		mCamera.focus(pivot);
		mGiValidationEvent = "pan";
	}
	if (mGiValidationFrame == 24)
	{
		mCamera.setOrbit(mCamera.yaw() + 1.3f, mCamera.pitch(), mCamera.distance());
		mGiValidationEvent = "cut";
	}
	if (mGiValidationFrame == 36)
	{
		mRaytracer.resetAccumulation();
		mGiValidationEvent = "reset";
	}
	if (mGiValidationFrame == 48)
	{
		mLighting.mDirectionalStrength += 1;
		mGiValidationEvent = "light";
	}
	if (mGiValidationFrame == 60)
	{
		mMaterials[0].mBaseColor[0] *= 0.8f;
		mRaytracer.updateMaterials(renderer, mMaterials);
		mGiValidationEvent = "material";
	}
	if (mGiValidationFrame == 72)
	{
		RECT rectangle{};
		if (!GetWindowRect(renderer.window(), &rectangle)
			|| !SetWindowPos(renderer.window(), nullptr, 0, 0, rectangle.right - rectangle.left + 128,
				rectangle.bottom - rectangle.top + 64, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE))
		{
			throw std::runtime_error("GI validation resize failed.");
		}
		mGiValidationEvent = "resize-request";
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
	ImGui::TextWrapped("Default: local Kitchen Set when available, otherwise the self-authored Preview Room.");
	if (ImGui::CollapsingHeader("Scene files"))
	{
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
	}
	ImGui::TextWrapped("%s", mStatus.c_str());
	if (mHasScene)
	{
		ImGui::Text("%llu triangles / %llu mesh occurrences", static_cast<unsigned long long>(mTriangleCount),
			static_cast<unsigned long long>(mHeader.mMeshCount));
		ImGui::Text("%zu meshlets / %llu unique vertices", mMeshletCount, static_cast<unsigned long long>(mHeader.mVertexCount));
		ImGui::Checkbox("Meshlet colors", &mMeshletColors);
	}
	if (ImGui::CollapsingHeader("Conversion log"))
	{
		ImGui::TextWrapped("%s", mLog.empty() ? "No conversion in this session." : mLog.c_str());
	}
	if (ImGui::Button("Frame model (F)"))
	{
		frameModel();
	}
	ImGui::SliderFloat("Exposure", &mExposure, 0.1f, 3.0f);
	if (!mMaterials.empty() && ImGui::CollapsingHeader("PBR material"))
	{
		ImGui::SliderInt("Material", &mSelectedMaterial, 0, static_cast<int>(mMaterials.size()) - 1, "%d", ImGuiSliderFlags_AlwaysClamp);
		mSelectedMaterial = std::clamp(mSelectedMaterial, 0, static_cast<int>(mMaterials.size()) - 1);
		auto material = mMaterials[mSelectedMaterial];
		bool changed = ImGui::ColorEdit3("Base Color (linear)", material.mBaseColor.data(), ImGuiColorEditFlags_Float);
		changed |= ImGui::SliderFloat("Metallic", &material.mMetallic, 0, 1, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		changed |= ImGui::SliderFloat("Roughness", &material.mRoughness, 0, 1, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		changed |= ImGui::SliderFloat("Specular", &material.mSpecular, 0, 1, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		changed |= ImGui::SliderFloat("Ambient Occlusion", &material.mOcclusion, 0, 1, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		changed |= ImGui::ColorEdit3("Emissive Color", material.mEmissiveColor.data(), ImGuiColorEditFlags_Float);
		changed |= ImGui::SliderFloat("Emissive Strength", &material.mEmissiveStrength, 0, 100000, "%.3f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
		ImGui::TextWrapped("Base Color multiplies vertex color. Roughness 0 is a mirror. Specular 0.5 gives dielectric F0 of 4%%.");
		if (ImGui::Button("Reset material"))
		{
			material = SceneMaterial{};
			changed = true;
		}
		if (changed)
		{
			try
			{
				auto replacement = mMaterials;
				replacement[mSelectedMaterial] = material;
				mRaytracer.updateMaterials(*mRenderer, replacement);
				mMaterials = std::move(replacement);
			}
			catch (const std::exception& error)
			{
				mStatus = error.what();
			}
		}
	}
	if (ImGui::CollapsingHeader("Rectangle lights", mLighting.mRectangles[mSelectedRectangle].mEnabled ? ImGuiTreeNodeFlags_DefaultOpen : 0))
	{
		ImGui::Text("Rectangles: %zu / %zu", mLighting.mRectangleCount, Lighting::maximumRectangles);
		ImGui::BeginDisabled(mLighting.mRectangleCount == Lighting::maximumRectangles);
		if (ImGui::Button("Add rectangle"))
		{
			mSelectedRectangle = static_cast<int>(mLighting.mRectangleCount++);
			mLighting.mRectangles[mSelectedRectangle] = {};
			mLighting.mRectangles[mSelectedRectangle].mEnabled = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Duplicate rectangle"))
		{
			auto copy = mLighting.mRectangles[mSelectedRectangle];
			copy.mPosition.x += copy.mWidth;
			mSelectedRectangle = static_cast<int>(mLighting.mRectangleCount++);
			mLighting.mRectangles[mSelectedRectangle] = copy;
		}
		ImGui::EndDisabled();
		ImGui::BeginDisabled(mLighting.mRectangleCount <= 1);
		if (ImGui::Button("Remove rectangle"))
		{
			for (size_t i = mSelectedRectangle; i + 1 < mLighting.mRectangleCount; ++i)
			{
				mLighting.mRectangles[i] = mLighting.mRectangles[i + 1];
			}
			--mLighting.mRectangleCount;
			mSelectedRectangle = std::min(mSelectedRectangle, static_cast<int>(mLighting.mRectangleCount) - 1);
		}
		ImGui::EndDisabled();
		ImGui::SliderInt("Rectangle index", &mSelectedRectangle, 0, static_cast<int>(mLighting.mRectangleCount) - 1);
		auto& rectangle = mLighting.mRectangles[mSelectedRectangle];
		ImGui::Checkbox("Enable rectangle", &rectangle.mEnabled);
		ImGui::Checkbox("Rectangle NEE / MIS", &mLighting.mRectangleMis);
		ImGui::DragFloat3("Rectangle position (m)", &rectangle.mPosition.x, mRadius * 0.01f);
		ImGui::SliderFloat("Rectangle width (m)", &rectangle.mWidth, 0.01f, 100.0f, "%.2f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Rectangle height (m)", &rectangle.mHeight, 0.01f, 100.0f, "%.2f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Rectangle azimuth", &rectangle.mAzimuth, -180.0f, 180.0f, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Rectangle elevation", &rectangle.mElevation, -90.0f, 90.0f, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
		ImGui::ColorEdit3("Rectangle color (linear)", rectangle.mColor.data(), ImGuiColorEditFlags_Float);
		ImGui::SliderFloat("Rectangle radiance", &rectangle.mRadiance, 0.0f, 10000.0f, "%.2f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
		ImGui::TextWrapped("One-sided emitter with an opaque back. Size changes total power at fixed radiance. Rectangle shadows are always enabled.");
	}
	if (ImGui::CollapsingHeader("ReSTIR DI"))
	{
		int mode = static_cast<int>(mTracing.mRestirMode);
		int debug = static_cast<int>(mTracing.mRestirDebug);
		ImGui::Combo("Light sampling", &mode, "NEE / MIS\0RIS (8 candidates)\0Temporal reuse\0Temporal + spatial reuse\0");
		ImGui::Combo("G-buffer view", &debug, "Lighting\0Normal\0Depth\0Motion\0Reservoir / history\0");
		mTracing.mRestirMode = static_cast<uint32_t>(mode);
		mTracing.mRestirDebug = static_cast<uint32_t>(debug);
		ImGui::TextWrapped("Primary IBL, emissive mesh and rectangle lighting. Secondary bounces use existing NEE / MIS.");
	}
	if (ImGui::CollapsingHeader("ReSTIR GI"))
	{
		int mode = static_cast<int>(mTracing.mGiMode);
		ImGui::Combo("GI sampling", &mode, "Ordinary PT\0Initial candidates\0Temporal (one previous raw sample)\0Temporal + spatial (four raw neighbors)\0PT temporal (full primary BSDF)\0PT hybrid (one glossy bounce)\0PT temporal reservoir\0PT spatial (one paired neighbor)\0PT spatiotemporal (one previous, one neighbor)\0");
		mTracing.mGiMode = static_cast<uint32_t>(mode);
		ImGui::Checkbox("Reuse previous GI candidate", &mTracing.mGiHistory);
		ImGui::Checkbox("Reuse neighboring GI candidates", &mTracing.mGiSpatial);
		int view = static_cast<int>(mTracing.mGiView);
		ImGui::Combo("GI view", &view, "Output\0Diffuse indirect\0Remainder\0Secondary radiance\0Secondary normal\0PDF (R: solid angle, G: area)\0Valid candidate\0Fresh reconstruction error x1000\0History (green: accepted, blue: selected)\0Diffuse indirect (noisy)\0Spatial (green: accepted, blue: selected)\0Hybrid work (R: replay, G: visibility, B: selected)\0Hybrid quality (R: contribution, G: PDF, B: state)\0PT reservoir (R: confidence, G: age, B: history selected)\0");
		mTracing.mGiView = static_cast<uint32_t>(view);
		ImGui::TextWrapped("Temporal and PT temporal reuse one previous raw candidate. PT temporal uses the full primary BSDF, including rough metals. Spatial mode reuses four current raw neighbors for diffuse GI.");
		ImGui::TextWrapped("PT hybrid and PT temporal reservoir replay one glossy bounce, then reconnect from a rough surface to a flat Lambertian vertex. The reservoir saves the selected path and its weight for the next frame. Diagnostic views bypass RR.");
		ImGui::TextWrapped("PT spatial reuses one current raw neighbor with the full primary BSDF. It uses one candidate buffer and no temporal history.");
		ImGui::TextWrapped("PT spatiotemporal adds one previous raw candidate to the spatial pair.");
		ImGui::TextWrapped("168 bytes/pixel per buffer. Modes with temporal history use two buffers; only the first sample is saved. Spatial modes add a one-path/pixel prepass. Changing lighting/materials, resize, reset or camera cuts discards history.");
	}
	if (ImGui::CollapsingHeader("DLSS Ray Reconstruction", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::BeginDisabled(!mRaytracer.rayReconstruction().supported());
		bool enabled = mRaytracer.rayReconstructionEnabled();
		if (ImGui::Checkbox("Ray Reconstruction (native / DLAA)", &enabled))
		{
			mRaytracer.setRayReconstructionEnabled(enabled);
		}
		ImGui::EndDisabled();
		int view = static_cast<int>(mRaytracer.rayReconstructionView());
		if (ImGui::Combo("RR view", &view, "Output\0Noisy HDR\0Normal\0Roughness\0Diffuse albedo\0Specular albedo\0Motion\0Depth\0Specular hit distance\0"))
		{
			mRaytracer.setRayReconstructionView(static_cast<uint32_t>(view));
		}
		ImGui::TextWrapped("%s", mRaytracer.rayReconstruction().status().c_str());
		ImGui::TextWrapped("RR receives the current noisy HDR frame. Long-term accumulation is used only with RR off.");
		ImGui::TextWrapped("G-buffer and meshlet views bypass RR.");
		ImGui::TextWrapped("Normal and other guides use pixel centers. Noisy HDR shows the jittered input.");
		if (mRaytracer.gpuMilliseconds() >= 0)
		{
			ImGui::Text("Scene GPU: %.2f ms", mRaytracer.gpuMilliseconds());
		}
	}
	if (ImGui::CollapsingHeader("Indirect lighting / IBL", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Text(mRaytracer.rayReconstructionEnabled() ? "Rendered samples (RR): %llu" : "Accumulated samples: %llu",
			static_cast<unsigned long long>(mRaytracer.accumulatedSamples()));
		int bounces = static_cast<int>(mTracing.mMaxBounces);
		int samples = static_cast<int>(mTracing.mSamplesPerFrame);
		ImGui::SliderInt("Path bounces", &bounces, 0, 8, "%d", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderInt("Samples per frame", &samples, 1, 16, "%d", ImGuiSliderFlags_AlwaysClamp);
		mTracing.mMaxBounces = static_cast<uint32_t>(std::clamp(bounces, 0, 8));
		mTracing.mSamplesPerFrame = static_cast<uint32_t>(std::clamp(samples, 1, 16));
		ImGui::SliderFloat("IBL intensity", &mTracing.mEnvironmentIntensity, 0.0f, 100.0f, "%.3f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("IBL rotation", &mTracing.mEnvironmentRotation, -180.0f, 180.0f, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
		ImGui::Checkbox("IBL NEE / MIS", &mTracing.mEnvironmentMis);
		ImGui::Checkbox("Emissive NEE / MIS", &mTracing.mEmissiveMis);
		ImGui::Checkbox("GGX multiple scattering", &mTracing.mMultipleScattering);
		ImGui::Checkbox("Russian roulette (after 3 bounces)", &mTracing.mRussianRoulette);
		ImGui::Text("Emissive triangles: %u (two-sided)", mRaytracer.emissiveTriangleCount());
		ImGui::TextWrapped("IBL: %s", mEnvironmentName.c_str());
		try
		{
			if (ImGui::Button("Open IBL (.hdr)..."))
			{
				const auto path = chooseFile(mRenderer->window(), L"Radiance HDR environment", L"*.hdr");
				if (!path.empty())
				{
					mRaytracer.loadEnvironment(*mRenderer, readRadianceEnvironment(path));
					mEnvironmentPath = path;
					mEnvironmentName = utf8(path.filename());
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Preview sky"))
			{
				mRaytracer.loadEnvironment(*mRenderer, createPreviewEnvironment());
				mEnvironmentPath.clear();
				mEnvironmentName = "Built-in preview sky";
			}
		}
		catch (const std::exception& error)
		{
			mStatus = error.what();
		}
		if (ImGui::Button("Reset accumulation"))
		{
			mRaytracer.resetAccumulation();
		}
	}
	if (ImGui::CollapsingHeader("Direct lighting"))
	{
		int type = mLighting.mPointLight ? 1 : 0;
		ImGui::Combo("Light type", &type, "Directional\0Point\0");
		mLighting.mPointLight = type == 1;
		ImGui::Checkbox("Shadows", &mLighting.mShadows);
		ImGui::ColorEdit3("Light color (linear)", mLighting.mColor.data(), ImGuiColorEditFlags_Float);
		if (mLighting.mPointLight)
		{
			ImGui::DragFloat3("Light position (m)", &mLighting.mPosition.x, mRadius * 0.01f);
			ImGui::SliderFloat("Point intensity", &mLighting.mPointStrength, 0.0f, 200.0f);
		}
		else
		{
			ImGui::Checkbox("Headlight", &mLighting.mHeadlight);
			ImGui::BeginDisabled(mLighting.mHeadlight);
			ImGui::SliderFloat("Light azimuth", &mLighting.mAzimuth, -180.0f, 180.0f, "%.1f deg");
			ImGui::SliderFloat("Light elevation", &mLighting.mElevation, -89.0f, 89.0f, "%.1f deg");
			ImGui::EndDisabled();
			ImGui::SliderFloat("Directional irradiance", &mLighting.mDirectionalStrength, 0.0f, 20.0f);
		}
		ImGui::SliderFloat("Ambient fill", &mLighting.mAmbient, 0.0f, 2.0f);
		ImGui::SliderFloat("Shadow bias (m)", &mLighting.mShadowBias, 0.000001f, 0.01f, "%.6f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
		ImGui::TextWrapped("Lambert diffuse + opaque hard shadows. Ambient fill is a preview aid, not indirect lighting.");
	}
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
		mRaytracer.render(renderer, mCamera, mViewportLeft, mLighting, mTracing, mExposure, mMeshletColors, commands, backBuffer);
		if (mGiValidationLog.is_open())
		{
			mGiValidationLog << mGiValidationFrame++ << ',' << mGiValidationEvent << ',' << mRaytracer.giHistoryUsedLastFrame()
				<< ',' << renderer.width() << ',' << renderer.height() << '\n';
			mGiValidationLog.flush();
			if (!mGiValidationLog)
			{
				throw std::runtime_error("Writing GI validation CSV failed.");
			}
		}
		if (mRaytracer.gpuMilliseconds() >= 0)
		{
			mScreenshotGpuMilliseconds += mRaytracer.gpuMilliseconds();
			if (mGpuTimingLog.is_open())
			{
				mGpuTimingLog << mRaytracer.gpuMilliseconds() << ',' << mRaytracer.accumulatedSamples() << '\n';
			}
		}
	}
}
