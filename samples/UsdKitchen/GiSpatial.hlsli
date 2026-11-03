// Current raw draw + one previous draw + four current-frame neighbors.
// All proposal inclusion tests depend only on primary surfaces, including null draws.
float3 spatialGi(GiCandidate current, uint2 pixel, uint sampleIndex, out float3 diagnostic, out float3 spatialDiagnostic)
{
	float3 fresh = current.reusable != 0 ? shadeGiCandidate(current) : 0;
	diagnostic = current.sourceValid != 0 ? float3(0.5f, 0.25f, 0) : 0;
	spatialDiagnostic = 0;
	if (current.sourceValid == 0)
	{
		return fresh;
	}
	uint2 dimensions = DispatchRaysDimensions().xy;
	GiCandidate proposals[6];
	proposals[0] = current;
	uint count = 1;
	uint temporalIndex = 6;
	if (giHistoryValid != 0)
	{
		diagnostic = float3(1, 0, 0);
		float2 previousPixel = reprojectPrimary(current.sourcePosition, previousViewProjection, float2(dimensions), previousViewportLeft);
		if (all(isfinite(previousPixel)) && all(previousPixel >= float2(previousViewportLeft, 0)) && all(previousPixel < float2(dimensions)))
		{
			uint2 previous = uint2(previousPixel);
			GiCandidate history = previousGiCandidates[previous.x + previous.y * dimensions.x];
			if (compatibleGiSources(current, history, 1))
			{
				temporalIndex = count;
				proposals[count++] = history;
				diagnostic = float3(0, 0.7f, 0);
			}
		}
	}
	const int2 offsets[4] = {int2(2, 0), int2(0, 2), int2(-2, 0), int2(0, -2)};
	uint neighbors = 0;
	for (uint i = 0; i < 4; ++i)
	{
		int2 neighborPixel = int2(pixel) + offsets[i];
		if (neighborPixel.x < int(ceil(viewportLeft)) || neighborPixel.y < 0 || any(neighborPixel >= int2(dimensions)))
		{
			continue;
		}
		uint2 address = uint2(neighborPixel);
		GiCandidate neighbor = giCandidates[address.x + address.y * dimensions.x];
		// Four-pixel world footprint leaves room for two-pixel offsets and jitter.
		if (compatibleGiSources(current, neighbor, 2))
		{
			proposals[count++] = neighbor;
			++neighbors;
		}
	}
	spatialDiagnostic = float3(0, float(neighbors) / 4, 0);
	float ratio = rcp(float(samplesPerFrame));
	uint state = (pixel.x + pixel.y * dimensions.x) ^ ((restirFrame + 1u) * 0x9e3779b9u)
		^ ((sampleIndex + 1u) * 0x85ebca6bu);
	float3 result = 0;
	uint selectedIndex = 6;
	for (uint j = 0; j < count; ++j)
	{
		GiCandidate lightSample = proposals[j];
		if (lightSample.valid == 0 || lightSample.reusable == 0 || maxComponent(lightSample.radiance) <= 0)
		{
			// This term is zero. Its proposal still appears in other denominators.
			continue;
		}
		float currentPdf = j == 0 ? current.areaPdf : giConnectionPdf(current, lightSample);
		if (currentPdf <= 0)
		{
			continue;
		}
		float denominator = currentPdf;
		for (uint k = 1; k < count; ++k)
		{
			float pdf = k == j ? lightSample.areaPdf : giConnectionPdf(proposals[k], lightSample);
			denominator += ratio * pdf;
		}
		Surface surface = giSourceSurface(current);
		float3 direction = normalize(lightSample.position - surface.position);
		float3 integrand = giDiffuseBrdfCosine(surface, current.sourceView, direction) * lightSample.radiance
			* giConnectionJacobian(lightSample, surface.position);
		float3 estimate = giBalanceEstimate(integrand * (j == 0 ? 1 : ratio), denominator, 0);
		bool selected;
		result = resampleGiPair(result, estimate, random(state), selected);
		if (selected)
		{
			selectedIndex = j;
		}
	}
	if (selectedIndex == temporalIndex && temporalIndex < count)
	{
		diagnostic.b = 1;
	}
	if (selectedIndex < count && selectedIndex != 0 && selectedIndex != temporalIndex)
	{
		spatialDiagnostic.b = 1;
	}
	return result;
}
