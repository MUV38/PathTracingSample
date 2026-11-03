#include "ClasSandbox.h"
#include "Sample.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
	ClasSandbox sandbox;
	Sample sample;
	sample.mName = L"CLAS Sandbox";
	sample.mRequireDxr = true;
	sample.mConfigure = [&sandbox](const std::vector<std::wstring>& arguments) { sandbox.configure(arguments); };
	sample.mInitialize = [&sandbox](Renderer& renderer) { sandbox.initialize(renderer); };
	sample.mUpdate = [&sandbox](Renderer& renderer, float deltaTime) { sandbox.update(renderer, deltaTime); };
	sample.mDrawGui = [&sandbox]() { sandbox.drawGui(); };
	sample.mRender = [&sandbox](Renderer& renderer, ID3D12GraphicsCommandList* commands, ID3D12Resource* backBuffer)
	{
		sandbox.render(renderer, commands, backBuffer);
	};
	return runSample(instance, showCommand, sample);
}
