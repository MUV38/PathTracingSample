Surface giSourceSurface(GiCandidate source)
{
	Material material = materials[source.sourceMaterial];
	Surface surface = (Surface)0;
	surface.position = source.sourcePosition;
	surface.normal = source.sourceNormal;
	surface.geometricNormal = source.sourceGeometricNormal;
	surface.baseColor = source.sourceBaseColor;
	surface.metallic = material.metallic;
	surface.roughness = material.roughness;
	surface.specular = material.specular;
	surface.occlusion = material.occlusion;
	surface.singleScatterOnly = multipleScattering == 0;
	return surface;
}

bool compatibleGiSources(GiCandidate current, GiCandidate previous, float footprintScale)
{
	float depth = dot(current.sourcePosition - cameraPosition, cameraForward);
	float footprint = 4 * max(depth, 0.001f) * tanHalfFov / float(DispatchRaysDimensions().y);
	return current.sourceValid != 0 && previous.sourceValid != 0
		&& current.sourceTriangle == previous.sourceTriangle && current.sourceMaterial == previous.sourceMaterial
		&& dot(current.sourceNormal, previous.sourceNormal) > 0.98f
		&& dot(current.sourceGeometricNormal, previous.sourceGeometricNormal) > 0.98f
		&& all(abs(current.sourceBaseColor - previous.sourceBaseColor) < 0.05f)
		&& distance(current.sourcePosition, previous.sourcePosition) <= max(footprint * footprintScale, 4 * shadowBias);
}

float giConnectionJacobian(GiCandidate lightSample, float3 sourcePosition)
{
	float3 delta = lightSample.position - sourcePosition;
	float distanceSquared = dot(delta, delta);
	if (lightSample.valid == 0 || lightSample.reusable == 0 || distanceSquared <= 1e-12f)
	{
		return 0;
	}
	// Treat opposite sides of a two-sided surface as separate domains.
	return max(dot(lightSample.normal, -delta * rsqrt(distanceSquared)), 0) / distanceSquared;
}

float giConnectionPdf(GiCandidate source, GiCandidate lightSample)
{
	float jacobian = giConnectionJacobian(lightSample, source.sourcePosition);
	if (source.sourceValid == 0 || jacobian <= 0)
	{
		return 0;
	}
	Surface surface = giSourceSurface(source);
	float3 direction = normalize(lightSample.position - surface.position);
	if (dot(surface.geometricNormal, direction) <= 0 || dot(surface.normal, direction) <= 0)
	{
		return 0;
	}
	float pdf;
	float3 bsdf = evaluateBsdf(surface, source.sourceView, direction, pdf);
	if (pdf <= 1e-10f || maxComponent(bsdf) <= 0 || !visibleToPoint(surface, lightSample.position))
	{
		return 0;
	}
	return pdf * jacobian;
}

#include "PtHybrid.hlsli"

float3 giFullPrimaryFactor(Surface surface, float3 view, float3 direction)
{
	float pdf;
	return evaluateBsdf(surface, view, direction, pdf) * saturate(dot(surface.normal, direction)) * surface.occlusion;
}

float3 temporalGi(GiCandidate current, uint2 pixel, uint sampleIndex, out float3 diagnostic, inout uint3 work)
{
	float3 fresh = current.reusable != 0 ? shadeGiCandidate(current) : 0;
	bool showShiftQuality = giView == 12;
	diagnostic = !showShiftQuality && current.sourceValid != 0 ? float3(0.5f, 0.25f, 0) : 0;
	if (giHistoryValid == 0 || current.sourceValid == 0)
	{
		return fresh;
	}
	float2 dimensions = float2(DispatchRaysDimensions().xy);
	float2 previousPixel = reprojectPrimary(current.sourcePosition, previousViewProjection, dimensions, previousViewportLeft);
	diagnostic = showShiftQuality ? float3(0, 0, 1.0f / 5) : float3(1, 0, 0);
	if (!all(isfinite(previousPixel)) || any(previousPixel < float2(previousViewportLeft, 0)) || any(previousPixel >= dimensions))
	{
		return fresh;
	}
	uint2 previous = uint2(previousPixel);
	GiCandidate history = previousGiCandidates[previous.x + previous.y * uint(dimensions.x)];
	if (!compatibleGiSources(current, history, 1))
	{
		return fresh;
	}
	// Keep the previous proposal even when its actual draw was a miss/black.
	// The selection of proposals depends on source surfaces, never on sample brightness.
	float3 currentEstimate = 0;
	// Only one raw draw is stored, even when the current frame traces several spp.
	float historyRatio = rcp(float(samplesPerFrame));
	bool skipBlackPaths = giEnabled == 5 && hybridUnoptimized == 0;
	if (current.valid != 0 && current.reusable != 0 && (!skipBlackPaths || any(current.radiance != 0)))
	{
		float3 unusedFactor;
		float previousPdf = giEnabled == 5 ? hybridConnectionPdf(history, current, unusedFactor, work) : giConnectionPdf(history, current);
		float jacobian = giEnabled == 5 ? current.areaPdf * current.inversePdf : giConnectionJacobian(current, current.sourcePosition);
		float3 integrand = current.diffuseBrdfCosine * current.radiance * jacobian;
		currentEstimate = giBalanceEstimate(integrand, current.areaPdf, previousPdf * historyRatio);
	}
	float3 historyEstimate = 0;
	bool reconnected = false;
	if (showShiftQuality)
	{
		bool hasCandidate = history.valid != 0 && history.reusable != 0 && any(history.radiance != 0);
		diagnostic = float3(0, 0, hasCandidate ? 3.0f / 5 : 2.0f / 5);
	}
	// Keep tracing for the History view, which visualizes connectivity even for black paths.
	if (history.valid != 0 && history.reusable != 0 && (!skipBlackPaths || giView == 8 || any(history.radiance != 0)))
	{
		float3 hybridFactor;
		float currentPdf = giEnabled == 5 ? hybridConnectionPdf(current, history, hybridFactor, work) : giConnectionPdf(current, history);
		if (currentPdf > 0 && giEnabled == 5)
		{
			reconnected = true;
			if (showShiftQuality && any(history.radiance != 0))
			{
				diagnostic = hybridShiftQuality(history, hybridFactor, currentPdf);
			}
			historyEstimate = giBalanceEstimate(hybridFactor * history.radiance, currentPdf, history.areaPdf * historyRatio) * historyRatio;
		}
		else if (currentPdf > 0)
		{
			Surface surface = giSourceSurface(current);
			float3 direction = normalize(history.position - surface.position);
			float3 factor = giDiffuseBrdfCosine(surface, current.sourceView, direction);
			if (giEnabled == 4)
			{
				factor = giFullPrimaryFactor(surface, current.sourceView, direction);
			}
			float3 integrand = factor * history.radiance * giConnectionJacobian(history, surface.position);
			historyEstimate = giBalanceEstimate(integrand, currentPdf, history.areaPdf * historyRatio) * historyRatio;
		}
	}
	// Separate RNG: resampling must not change subsequent PT paths or spp.
	uint state = (pixel.x + pixel.y * uint(dimensions.x)) ^ ((restirFrame + 1u) * 0x9e3779b9u)
		^ ((sampleIndex + 1u) * 0x85ebca6bu) ^ (sampleSeed * 0xc2b2ae35u);
	bool selectedPrevious;
	float3 result = resampleGiPair(currentEstimate, historyEstimate, random(state), selectedPrevious);
	if (giView == 11 && selectedPrevious)
	{
		++work.z;
	}
	if (!showShiftQuality)
	{
		diagnostic = giEnabled == 5 && !reconnected ? float3(0.5f, 0, 0.5f) : float3(0, 0.7f, selectedPrevious ? 1 : 0);
	}
	return result;
}
