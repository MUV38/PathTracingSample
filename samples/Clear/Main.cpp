#include "Sample.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
	Sample sample;
	sample.mName = L"Clear / ImGui";
	return runSample(instance, showCommand, sample);
}
