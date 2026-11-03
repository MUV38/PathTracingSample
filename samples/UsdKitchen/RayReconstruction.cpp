#include "RayReconstruction.h"
#include <pix3.h>
#include "Sample.h"
#include <filesystem>
#include <cstring>
#include <cmath>
#include <stdexcept>
#pragma warning(push, 0)
#include <sl_security.h>
#pragma warning(pop)

namespace
{
void check(sl::Result result, const char* operation)
{
	if (result != sl::Result::eOk)
	{
		throw std::runtime_error(std::string(operation) + " failed (Streamline result " + std::to_string(static_cast<int>(result)) + "). See Streamline/logs.");
	}
}
void checkHr(HRESULT result, const char* operation)
{
	if (FAILED(result))
	{
		throw std::runtime_error(operation);
	}
}
sl::float4x4 matrix(DirectX::FXMMATRIX value)
{
	DirectX::XMFLOAT4X4 stored;
	DirectX::XMStoreFloat4x4(&stored, value);
	sl::float4x4 result;
	static_assert(sizeof(result) == sizeof(stored));
	std::memcpy(&result, &stored, sizeof(result));
	return result;
}
}

RayReconstruction* RayReconstruction::sInstance = nullptr;
void RayReconstruction::logMessage(sl::LogType type, const char* message)
{
	OutputDebugStringA(message);
	if (type == sl::LogType::eError && sInstance && sInstance->mInEvaluation.load())
	{
		sInstance->mEvaluationFailed = true;
	}
}
RayReconstruction::~RayReconstruction()
{
	shutdown();
	for (auto& resource : mResources)
	{
		resource.Reset();
	}
	if (mModule)
	{
		FreeLibrary(mModule);
	}
	sInstance = nullptr;
}
void RayReconstruction::start()
{
	const auto directory = executableDirectory() / L"Streamline";
	const auto path = directory / L"sl.interposer.dll";
	if (!std::filesystem::exists(path))
	{
		mStatus = "Streamline DLLs are missing. Run tools/RestoreStreamline.ps1 and rebuild.";
		return;
	}
	if (!sl::security::verifyEmbeddedSignature(path.c_str()))
	{
		throw std::runtime_error("Streamline DLL signature validation failed.");
	}
	mModule = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
	if (!mModule)
	{
		throw std::runtime_error("Cannot load sl.interposer.dll.");
	}
#define LOAD_SL(name) \
	name = reinterpret_cast<PFun_##name*>(GetProcAddress(mModule, #name)); \
	if (!name) \
	{ \
		throw std::runtime_error("Missing Streamline API: " #name); \
	}
	LOAD_SL(slInit)
	LOAD_SL(slShutdown)
	LOAD_SL(slSetD3DDevice)
	LOAD_SL(slIsFeatureSupported)
	LOAD_SL(slGetFeatureFunction)
	LOAD_SL(slGetNewFrameToken)
	LOAD_SL(slSetConstants)
	LOAD_SL(slSetTagForFrame)
	LOAD_SL(slEvaluateFeature)
	LOAD_SL(slFreeResources)
#undef LOAD_SL
	mCreateFactory = reinterpret_cast<decltype(mCreateFactory)>(GetProcAddress(mModule, "CreateDXGIFactory2"));
	mCreateDevice = reinterpret_cast<decltype(mCreateDevice)>(GetProcAddress(mModule, "D3D12CreateDevice"));
	if (!mCreateFactory || !mCreateDevice)
	{
		throw std::runtime_error("Streamline graphics entry points are missing.");
	}
	const sl::Feature features[] = {sl::kFeatureDLSS_RR};
	const auto logs = directory / L"logs";
	std::filesystem::create_directories(logs);
	const auto pluginPath = directory.wstring();
	const wchar_t* paths[] = {pluginPath.c_str()};
	const auto logPath = logs.wstring();
	sl::Preferences preferences;
	preferences.featuresToLoad = features;
	preferences.numFeaturesToLoad = 1;
	preferences.pathsToPlugins = paths;
	preferences.numPathsToPlugins = 1;
	preferences.pathToLogsAndData = logPath.c_str();
	preferences.flags = sl::PreferenceFlags::eDisableCLStateTracking | sl::PreferenceFlags::eUseFrameBasedResourceTagging;
	preferences.engine = sl::EngineType::eCustom;
	preferences.engineVersion = "1.0";
	preferences.projectId = "f4680a75-5d93-4afb-a63a-79dc02ccb6c8";
	preferences.renderAPI = sl::RenderAPI::eD3D12;
	sInstance = this;
	preferences.logMessageCallback = logMessage;
	check(slInit(preferences, sl::kSDKVersion), "slInit");
	mStarted = true;
	mStatus = "Streamline 2.14.16 initialized; checking GPU support.";
}
HRESULT RayReconstruction::createFactory(UINT flags, REFIID iid, void** factory)
{
	return mStarted ? mCreateFactory(flags, iid, factory) : CreateDXGIFactory2(flags, iid, factory);
}
HRESULT RayReconstruction::createDevice(IUnknown* adapter, D3D_FEATURE_LEVEL level, REFIID iid, void** device)
{
	const auto result = mStarted ? mCreateDevice(adapter, level, iid, device) : D3D12CreateDevice(adapter, level, iid, device);
	if (mStarted && SUCCEEDED(result) && device && *device)
	{
		// Register before SampleFramework creates queues, fences or the swap chain.
		check(slSetD3DDevice(*device), "slSetD3DDevice");
	}
	return result;
}
void RayReconstruction::setDevice(ID3D12Device* device)
{
	if (!mStarted)
	{
		return;
	}
	auto luid = device->GetAdapterLuid();
	sl::AdapterInfo adapter;
	adapter.deviceLUID = reinterpret_cast<uint8_t*>(&luid);
	adapter.deviceLUIDSizeInBytes = sizeof(luid);
	const auto result = slIsFeatureSupported(sl::kFeatureDLSS_RR, adapter);
	if (result != sl::Result::eOk)
	{
		mStatus = "DLSS Ray Reconstruction unavailable (result " + std::to_string(static_cast<int>(result)) + "). See Streamline/logs.";
		return;
	}
	check(slGetFeatureFunction(sl::kFeatureDLSS_RR, "slDLSSDSetOptions", reinterpret_cast<void*&>(mSetOptions)), "Load RR options API");
	mSupported = true;
	mStatus = "DLSS Ray Reconstruction supported (native resolution / DLAA).";
}
void RayReconstruction::shutdown()
{
	if (mStarted)
	{
		if (mAllocated)
		{
			slFreeResources(sl::kFeatureDLSS_RR, sl::ViewportHandle(0));
			mAllocated = false;
		}
		slShutdown();
		mStarted = false;
		mSupported = false;
	}
}
DXGI_FORMAT RayReconstruction::format(size_t index)
{
	constexpr DXGI_FORMAT formats[] = {DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R32_FLOAT,
		DXGI_FORMAT_R32G32_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT,
		DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT};
	return formats[index];
}
void RayReconstruction::resize(ID3D12Device* device, UINT width, UINT height)
{
	if (mWidth == width && mHeight == height)
	{
		return;
	}
	if (mAllocated)
	{
		check(slFreeResources(sl::kFeatureDLSS_RR, sl::ViewportHandle(0)), "Free resized RR history");
		mAllocated = false;
	}
	D3D12_HEAP_PROPERTIES heap{};
	heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = width;
	desc.Height = height;
	desc.DepthOrArraySize = desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	constexpr const wchar_t* names[] = {L"RR noisy HDR", L"RR linear depth", L"RR motion vectors",
		L"RR normal / roughness", L"RR diffuse albedo", L"RR specular albedo", L"RR specular hit distance", L"RR output HDR"};
	for (size_t index = 0; index < mResources.size(); ++index)
	{
		desc.Format = format(index);
		mResources[index].Reset();
		checkHr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&mResources[index])), "Create RR guide texture");
		mResources[index]->SetName(names[index]);
	}
	mWidth = width;
	mHeight = height;
	mLeft = UINT_MAX;
}
void RayReconstruction::evaluate(ID3D12GraphicsCommandList* commands, const Camera& camera, UINT left,
	DirectX::XMFLOAT2 jitter, const DirectX::XMFLOAT4X4& previousViewProjection, bool reset, uint32_t frame)
{
	PIXScopedEvent(commands, PIX_COLOR(110, 200, 80), "DLSS Ray Reconstruction");
	const sl::ViewportHandle viewport(0);
	if (mLeft != left && mAllocated)
	{
		check(slFreeResources(sl::kFeatureDLSS_RR, viewport), "Free changed RR viewport");
		mAllocated = false;
	}
	reset = reset || !mAllocated;
	mLeft = left;
	using namespace DirectX;
	const auto pos = camera.position();
	const auto forward = camera.forward();
	const auto up = camera.up();
	const auto right = camera.right();
	const float aspect = float(mWidth) / float(mHeight);
	const auto view = XMMatrixLookToRH(XMLoadFloat3(&pos), XMLoadFloat3(&forward), XMLoadFloat3(&up));
	const auto projection = XMMatrixPerspectiveFovRH(2 * std::atan(camera.tanHalfFov()), aspect, 0.001f, 100000.0f);
	const auto inverseVP = XMMatrixInverse(nullptr, view * projection);
	const auto clipToPrevious = reset ? XMMatrixIdentity() : inverseVP * XMLoadFloat4x4(&previousViewProjection);
	sl::DLSSDOptions options;
	options.mode = sl::DLSSMode::eDLAA;
	options.outputWidth = mWidth;
	options.outputHeight = mHeight;
	options.colorBuffersHDR = sl::Boolean::eTrue;
	options.normalRoughnessMode = sl::DLSSDNormalRoughnessMode::ePacked;
	options.worldToCameraView = matrix(view);
	options.cameraViewToWorld = matrix(XMMatrixInverse(nullptr, view));
	check(mSetOptions(viewport, options), "Set RR options");
	sl::FrameToken* token = nullptr;
	check(slGetNewFrameToken(token, &frame), "Get RR frame token");
	sl::Constants constants;
	constants.cameraViewToClip = matrix(projection);
	constants.clipToCameraView = matrix(XMMatrixInverse(nullptr, projection));
	constants.clipToPrevClip = matrix(clipToPrevious);
	constants.prevClipToClip = matrix(XMMatrixInverse(nullptr, clipToPrevious));
	// Rays sample pixelCenter + jitter. Geometry therefore moves by -jitter
	// in the input image, which is the projection offset expected by DLSS.
	constants.jitterOffset = {-jitter.x, -jitter.y};
	constants.mvecScale = {1.0f / float(mWidth), 1.0f / float(mHeight)};
	constants.cameraPinholeOffset = {0,0};
	constants.cameraPos = {pos.x,pos.y,pos.z};
	constants.cameraUp = {up.x,up.y,up.z};
	constants.cameraRight = {right.x,right.y,right.z};
	constants.cameraFwd = {forward.x,forward.y,forward.z};
	constants.cameraNear = 0.001f;
	constants.cameraFar = 100000;
	constants.cameraFOV = 2 * std::atan(camera.tanHalfFov());
	constants.cameraAspectRatio = aspect;
	constants.depthInverted = sl::Boolean::eFalse;
	constants.cameraMotionIncluded = sl::Boolean::eTrue;
	constants.motionVectors3D = sl::Boolean::eFalse;
	constants.motionVectorsJittered = sl::Boolean::eFalse;
	constants.reset = reset ? sl::Boolean::eTrue : sl::Boolean::eFalse;
	check(slSetConstants(constants, *token, viewport), "Set RR camera constants");
	const sl::BufferType types[] = {sl::kBufferTypeScalingInputColor, sl::kBufferTypeLinearDepth,
		sl::kBufferTypeMotionVectors, sl::kBufferTypeNormalRoughness, sl::kBufferTypeAlbedo,
		sl::kBufferTypeSpecularAlbedo, sl::kBufferTypeSpecularHitDistance, sl::kBufferTypeScalingOutputColor};
	std::array<sl::Resource,8> resources;
	std::array<sl::ResourceTag,8> tags;
	const sl::Extent extent{0, 0, mWidth, mHeight};
	D3D12_RESOURCE_BARRIER uav{};
	uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	commands->ResourceBarrier(1, &uav);
	for (size_t i = 0; i < resources.size(); ++i)
	{
		resources[i] = sl::Resource(sl::ResourceType::eTex2d, mResources[i].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		tags[i] = sl::ResourceTag(&resources[i], types[i], sl::ResourceLifecycle::eOnlyValidNow, &extent);
	}
	check(slSetTagForFrame(*token, viewport, tags.data(), static_cast<uint32_t>(tags.size()), commands), "Tag RR input textures");
	const sl::BaseStructure* inputs[] = {&viewport};
	mEvaluationFailed = false;
	mInEvaluation = true;
	const auto result = slEvaluateFeature(sl::kFeatureDLSS_RR, *token, inputs, 1, commands);
	mInEvaluation = false;
	check(result, "Evaluate DLSS Ray Reconstruction");
	if (mEvaluationFailed.load())
	{
		throw std::runtime_error("DLSS Ray Reconstruction evaluation logged an error. See Streamline/logs/sl.log.");
	}
	commands->ResourceBarrier(1, &uav);
	mAllocated = true;
}
