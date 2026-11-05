#include "PtlasExperiment.h"

#include <imgui.h>

void PtlasExperiment::initialize(const ClasContext& context)
{
	// TODO(user): prepare partition and instance data using context.mReferenceInstances.
	// Check context.mCapabilities.available() before using preview APIs.
	(void)context;
}

void PtlasExperiment::record(const ClasContext& context, ID3D12GraphicsCommandList* commands)
{
	// TODO(user): build or update the partitioned TLAS and order its writes before tracing.
	(void)context;
	(void)commands;
}

void PtlasExperiment::drawGui()
{
	ImGui::TextWrapped("PTLAS implementation: TODO in PtlasExperiment.cpp");
}

ClasTraceState PtlasExperiment::traceState() const
{
	// Return the TLAS, compatible pipeline and shader table when ready.
	return {};
}
