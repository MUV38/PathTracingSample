// The sample domain is (primary BSDF random triplet, reconnect vertex area).
// Replay preserves the triplet, so only the connection changes measure.
float hybridConnectionPdf(GiCandidate source, GiCandidate path, out float3 factor, inout uint3 work)
{
	factor = 0;
	Surface primary = giSourceSurface(source);
	if (path.prefixSample.z >= specularProbability(primary, source.sourceView))
	{
		return 0;
	}
	float3 direction, prefixWeight;
	if (!sampleBsdf(primary, source.sourceView, path.prefixSample, direction, prefixWeight))
	{
		return 0;
	}
	RayDesc ray;
	ray.Origin = offsetOrigin(primary.position, primary.geometricNormal);
	ray.Direction = direction;
	ray.TMin = 0;
	ray.TMax = 100000;
	Payload payload;
	payload.hit = 0;
	if (giView == 11)
	{
		++work.x;
	}
	TraceRay(scene, RAY_FLAG_NONE, 0xff, 0, 0, 0, ray, payload);
	if (payload.hit == 0 || payload.triangleIndex != path.prefixTriangle)
	{
		return 0;
	}
	ray.TMax = dot(payload.position - ray.Origin, direction);
	if (rectangleOccludes(ray))
	{
		return 0;
	}
	Surface middle = pathSurface(payload, materials[payload.materialIndex]);
	float jacobian = giConnectionJacobian(path, middle.position);
	if (middle.roughness < 0.2f || jacobian <= 0 || (giEnabled == 6 && middle.occlusion <= 0))
	{
		return 0;
	}
	float3 connection = normalize(path.position - middle.position);
	if (dot(middle.geometricNormal, connection) <= 0 || dot(middle.normal, connection) <= 0)
	{
		return 0;
	}
	float pdf;
	float3 bsdf = evaluateBsdf(middle, -direction, connection, pdf);
	if (pdf <= 1e-10f || maxComponent(bsdf) <= 0)
	{
		return 0;
	}
	if (giView == 11)
	{
		++work.y;
	}
	if (!visibleToPoint(middle, path.position))
	{
		return 0;
	}
	factor = prefixWeight * bsdf * saturate(dot(middle.normal, connection)) * middle.occlusion * jacobian;
	return pdf * jacobian;
}

float3 hybridShiftQuality(GiCandidate history, float3 shiftedFactor, float shiftedPdf)
{
	const float3 luminance = float3(0.2126f, 0.7152f, 0.0722f);
	float oldJacobian = history.areaPdf * history.inversePdf;
	float oldContribution = dot(history.diffuseBrdfCosine * history.radiance * oldJacobian, luminance);
	float newContribution = dot(shiftedFactor * history.radiance, luminance);
	if (oldContribution <= 0 || newContribution < 0 || history.areaPdf <= 0 || shiftedPdf <= 0
		|| !all(isfinite(float4(oldContribution, newContribution, history.areaPdf, shiftedPdf))))
	{
		return float3(0, 0, 1); // Valid connection, ratio undefined.
	}
	float contributionStops = newContribution > 0 ? log2(newContribution) - log2(oldContribution) : -8;
	float pdfStops = log2(shiftedPdf) - log2(history.areaPdf);
	return float3(saturate(0.5f + float2(contributionStops, pdfStops) / 16), 4.0f / 5);
}
