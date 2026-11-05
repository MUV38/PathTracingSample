#pragma once

#include "ClasContext.h"

// User implementation boundary. No CLAS build/assembly/shading is implemented.
class ClasExperiment final
{
public:
	void initialize(const ClasContext& context);
	void record(const ClasContext& context, ID3D12GraphicsCommandList* commands);
	void drawGui();
	ClasTraceState traceState() const;
};
