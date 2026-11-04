float3 temporalPtReservoir(GiCandidate current, uint2 pixel, uint sampleIndex, out GiCandidate stored, out float3 diagnostic)
{
	stored = current;
	stored.historyAge = 0;
	stored.contributionWeight = current.valid != 0 && current.reusable != 0 ? rcp(current.areaPdf) : 0;
	stored.confidence = current.sourceValid != 0 ? 1 : 0;
	float3 fresh = current.reusable != 0 ? shadeGiCandidate(current) : 0;
	diagnostic = current.sourceValid != 0 ? float3(0.5f, 0.25f, 0) : 0;
	if (giHistoryValid == 0 || current.sourceValid == 0)
	{
		return fresh;
	}
	uint2 dimensions = DispatchRaysDimensions().xy;
	float2 previousPixel = reprojectPrimary(current.sourcePosition, previousViewProjection, dimensions, previousViewportLeft);
	diagnostic = float3(1, 0, 0);
	if (!all(isfinite(previousPixel)) || any(previousPixel < float2(previousViewportLeft, 0)) || any(previousPixel >= dimensions))
	{
		return fresh;
	}
	uint2 address = uint2(previousPixel);
	GiCandidate history = previousGiCandidates[address.x + address.y * dimensions.x];
	if (!compatibleGiSources(current, history, 1) || history.confidence <= 0)
	{
		return fresh;
	}
	// Partition the joint (prefix random triplet, endpoint area) domain by support.
	// Confidence depends on source compatibility, not on the selected path's value.
	float alpha = min(history.confidence, float(ptReservoirConfidence - 1u)) / float(samplesPerFrame);
	float historyShare = alpha / (1 + alpha);
	uint3 unusedWork = 0;
	float3 currentF = current.diffuseBrdfCosine * current.radiance * (current.areaPdf * current.inversePdf);
	float currentWeight = stored.contributionWeight;
	if (currentWeight > 0 && any(currentF != 0))
	{
		float3 unusedFactor;
		if (hybridConnectionPdf(history, current, unusedFactor, unusedWork) > 0)
		{
			currentWeight /= 1 + alpha;
		}
	}
	float3 historyFactor = 0;
	float shiftedPdf = 0;
	if (history.contributionWeight > 0 && any(history.radiance != 0))
	{
		shiftedPdf = hybridConnectionPdf(current, history, historyFactor, unusedWork);
	}
	// The shift preserves the triplet and endpoint area. Carry the random contribution
	// weight forward; shiftedPdf is a support test, not the reservoir's sampling PDF.
	float3 historyF = historyFactor * history.radiance;
	const float3 luminance = float3(0.2126f, 0.7152f, 0.0722f);
	float a = dot(max(currentF, 0), luminance) * currentWeight;
	float b = dot(max(historyF, 0), luminance) * (history.contributionWeight * historyShare);
	float sum = a + b;
	if (!isfinite(sum))
	{
		return fresh;
	}
	stored.confidence = 1 + alpha;
	if (sum <= 0)
	{
		stored.contributionWeight = 0;
		diagnostic = float3(0, 0.7f, 0);
		return 0;
	}
	uint state = (pixel.x + pixel.y * dimensions.x) ^ ((restirFrame + 1u) * 0x9e3779b9u)
		^ ((sampleIndex + 1u) * 0x85ebca6bu) ^ (sampleSeed * 0xc2b2ae35u);
	bool selectHistory = b > 0 && (a == 0 || random(state) * sum < b);
	float3 selectedF = selectHistory ? historyF : currentF;
	float selectedWeight = sum / dot(max(selectedF, 0), luminance);
	if (!isfinite(selectedWeight))
	{
		stored.confidence = 1;
		return fresh;
	}
	if (selectHistory)
	{
		stored.historyAge = min(history.historyAge + 1u, 32u);
		stored.position = history.position;
		stored.normal = history.normal;
		stored.radiance = history.radiance;
		stored.valid = history.valid;
		stored.reusable = history.reusable;
		stored.prefixSample = history.prefixSample;
		stored.prefixTriangle = history.prefixTriangle;
		stored.areaPdf = shiftedPdf;
	}
	stored.contributionWeight = selectedWeight;
	diagnostic = float3(0, 0.7f, selectHistory ? 1 : 0);
	return selectedF * selectedWeight;
}
