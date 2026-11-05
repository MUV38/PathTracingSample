#include "ClasSandbox.h"
#include "ClasExperiment.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
	ClasExperiment experiment;
	return runClasSandbox(instance, showCommand, {L"CLAS Sandbox", "CLAS Sandbox"}, bindClasExperiment(experiment));
}
