#include "ClusterTemplatesExperiment.h"

#include <imgui.h>

void ClusterTemplatesExperiment::initialize(const ClasContext& context)
{
	// TODO(user): choose a template topology and prepare meshlet vertex/index inputs.
	// Check context.mCapabilities.available() before using preview APIs.
	(void)context;
}

void ClusterTemplatesExperiment::record(const ClasContext& context, ID3D12GraphicsCommandList* commands)
{
	// TODO(user): build templates, instantiate CLAS, then build Cluster BLAS and TLAS.
	(void)context;
	(void)commands;
}

void ClusterTemplatesExperiment::drawGui()
{
	ImGui::TextWrapped("Cluster Templates implementation: TODO in ClusterTemplatesExperiment.cpp");
}

ClasTraceState ClusterTemplatesExperiment::traceState() const
{
	// Return the TLAS, compatible pipeline and shader table when ready.
	return {};
}
