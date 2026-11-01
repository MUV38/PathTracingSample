#include "DxrCube.h"
#include "Sample.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
	DxrCube cube;
	Sample sample;
	sample.mName = L"DXR Cube";
	sample.mRequireDxr = true;
	sample.mInitialize = [&cube](Renderer& renderer) { cube.initialize(renderer); };
	sample.mUpdate = [&cube](Renderer& renderer, float deltaTime) { cube.update(renderer, deltaTime); };
	sample.mDrawGui = [&cube]() { cube.drawGui(); };
	sample.mRender = [&cube](Renderer& renderer, ID3D12GraphicsCommandList* commands, ID3D12Resource* backBuffer)
	{
		cube.render(renderer, commands, backBuffer);
	};
	return runSample(instance, showCommand, sample);
}
