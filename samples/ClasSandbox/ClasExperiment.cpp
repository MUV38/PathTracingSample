#include "ClasExperiment.h"

#include <imgui.h>

bool ClasCapabilities::available() const
{
	return SUCCEEDED(mEnablePreview) && SUCCEEDED(mFeatureQuery) && mClustersSupported
		&& SUCCEEDED(mDeviceQuery) && SUCCEEDED(mCommandListQuery)
		&& SUCCEEDED(mShaderModelQuery) && mShaderModel610;
}

bool ClasTraceState::ready() const
{
	return mTlas && mPipeline && mShaderTable;
}

void ClasExperiment::initialize(const ClasContext& context)
{
	// TODO(user): query prebuild sizes, allocate resources and prepare CLAS inputs.
	// context contains CPU meshlets, GPU buffers, the device/queue and shared root signature.
	// Unsupported hardware still enters here; guard preview operations with available().
	(void)context;
}

void ClasExperiment::record(const ClasContext& context, ID3D12GraphicsCommandList* commands)
{
	// TODO(user): record CLAS -> Cluster BLAS -> TLAS builds and required barriers.
	// Called before traceState() on every supported frame, including Reference mode.
	(void)context;
	(void)commands;
}

void ClasExperiment::drawGui()
{
	ImGui::TextWrapped("CLAS implementation: TODO in ClasExperiment.cpp");
}

ClasTraceState ClasExperiment::traceState() const
{
	// TODO(user): return owned TLAS, CLAS-enabled pipeline and shader table when ready.
	return {};
}
