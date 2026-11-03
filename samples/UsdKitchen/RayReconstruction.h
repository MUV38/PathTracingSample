#pragma once

#include "Camera.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <sl.h>
#include <sl_dlss_d.h>
#include <array>
#include <atomic>
#include <string>

class RayReconstruction final
{
public:
	~RayReconstruction();
	void start();
	void setDevice(ID3D12Device* device);
	void shutdown();
	bool supported() const { return mSupported; }
	const std::string& status() const { return mStatus; }
	HRESULT createFactory(UINT flags, REFIID iid, void** factory);
	HRESULT createDevice(IUnknown* adapter, D3D_FEATURE_LEVEL level, REFIID iid, void** device);
	void resize(ID3D12Device* device, UINT width, UINT height);
	ID3D12Resource* resource(size_t index) const { return mResources[index].Get(); }
	void evaluate(ID3D12GraphicsCommandList* commands, const Camera& camera, UINT left, DirectX::XMFLOAT2 jitter,
		const DirectX::XMFLOAT4X4& previousViewProjection, bool reset, uint32_t frame);
	static DXGI_FORMAT format(size_t index);

private:
	static void logMessage(sl::LogType type, const char* message);
	static RayReconstruction* sInstance;
	std::atomic<bool> mInEvaluation = false;
	std::atomic<bool> mEvaluationFailed = false;
	template<typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;
	HMODULE mModule = nullptr;
	bool mStarted = false;
	bool mSupported = false;
	bool mAllocated = false;
	std::string mStatus = "Streamline SDK is not loaded.";
	std::array<ComPtr<ID3D12Resource>,8> mResources;
	UINT mWidth = 0;
	UINT mHeight = 0;
	UINT mLeft = UINT_MAX;
	SL_FUN_DECL(slInit);
	SL_FUN_DECL(slShutdown);
	SL_FUN_DECL(slSetD3DDevice);
	SL_FUN_DECL(slIsFeatureSupported);
	SL_FUN_DECL(slGetFeatureFunction);
	SL_FUN_DECL(slGetNewFrameToken);
	SL_FUN_DECL(slSetConstants);
	SL_FUN_DECL(slSetTagForFrame);
	SL_FUN_DECL(slEvaluateFeature);
	SL_FUN_DECL(slFreeResources);
	PFun_slDLSSDSetOptions* mSetOptions = nullptr;
	decltype(&CreateDXGIFactory2) mCreateFactory = nullptr;
	decltype(&D3D12CreateDevice) mCreateDevice = nullptr;
};
