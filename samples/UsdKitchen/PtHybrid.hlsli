// The sample domain is (primary BSDF random triplet, reconnect vertex area).
// Replay preserves the triplet, so only the connection changes measure.
float hybridConnectionPdf(GiCandidate source, GiCandidate path, out float3 factor)
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
	if (middle.roughness < 0.2f || jacobian <= 0)
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
	if (pdf <= 1e-10f || maxComponent(bsdf) <= 0 || !visibleToPoint(middle, path.position))
	{
		return 0;
	}
	factor = prefixWeight * bsdf * saturate(dot(middle.normal, connection)) * middle.occlusion * jacobian;
	return pdf * jacobian;
}
