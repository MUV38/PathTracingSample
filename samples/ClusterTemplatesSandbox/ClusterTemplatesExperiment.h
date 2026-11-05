#pragma once

#include "../ClasSandbox/ClasContext.h"

class ClusterTemplatesExperiment final
{
public:
	void initialize(const ClasContext& context);
	void record(const ClasContext& context, ID3D12GraphicsCommandList* commands);
	void drawGui();
	ClasTraceState traceState() const;
};
