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
	sample.mUpdate = [&viewer](Renderer& renderer, float deltaTime) { viewer.update(renderer, deltaTime); };
	sample.mDrawGui = [&viewer]() { viewer.drawGui(); };
	sample.mRender = [&viewer](Renderer& renderer, ID3D12GraphicsCommandList* commands, ID3D12Resource* backBuffer)
	{
		viewer.render(renderer, commands, backBuffer);
	};
	return runSample(instance, showCommand, sample);
}
