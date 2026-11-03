#include "UsdKitchen.h"
#include "Sample.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
	UsdKitchen viewer;
	Sample sample;
	sample.mName = L"USD Kitchen / Binary Scene";
	sample.mRequireDxr = true;
	sample.mConfigure = [&viewer](const std::vector<std::wstring>& arguments) { viewer.configure(arguments); };
	sample.mReadyForScreenshot = [&viewer]() { return viewer.readyForScreenshot(); };
	sample.mInitialize = [&viewer](Renderer& renderer) { viewer.initialize(renderer); };
	sample.mCreateFactory = [&viewer](UINT flags, REFIID iid, void** factory)
	{
		return viewer.rayReconstruction().createFactory(flags, iid, factory);
	};
	sample.mCreateDevice = [&viewer](IUnknown* adapter, D3D_FEATURE_LEVEL level, REFIID iid, void** device)
	{
		return viewer.rayReconstruction().createDevice(adapter, level, iid, device);
	};
	sample.mShutdown = [&viewer]() { viewer.rayReconstruction().shutdown(); };
	sample.mUpdate = [&viewer](Renderer& renderer, float deltaTime) { viewer.update(renderer, deltaTime); };
	sample.mDrawGui = [&viewer]() { viewer.drawGui(); };
	sample.mRender = [&viewer](Renderer& renderer, ID3D12GraphicsCommandList* commands, ID3D12Resource* backBuffer)
	{
		viewer.render(renderer, commands, backBuffer);
	};
	return runSample(instance, showCommand, sample);
}
