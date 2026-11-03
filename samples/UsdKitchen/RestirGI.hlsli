// Initial diffuse GI candidates only. No temporal/spatial resampling yet.
// This stage preserves the original BRDF-mixture proposal and its full PDF.
struct GiCandidate
{
	float3 position;
	float solidAnglePdf;
	float3 normal;
	float areaPdf;
	float3 radiance;
	uint valid;
	float3 sourcePosition;
	uint sourceTriangle;
	float3 diffuseBrdfCosine;
	float inversePdf;
	float3 direction;
	float targetDensity;
}; // 96 bytes; renderer allocation must match.

float3 giDiffuseBrdfCosine(Surface surface, float3 view, float3 direction)
{
	float noV = dot(surface.normal, view);
	float noL = dot(surface.normal, direction);
	if (surface.roughness < 0.2f || noV <= 0 || noL <= 0 || dot(surface.geometricNormal, direction) <= 0)
	{
		return 0;
	}
	// Only the dielectric diffuse term. GGX compensation stays in ordinary PT.
	float3 transmissionV = 1 - fresnelSchlick(materialF0(surface), noV);
	float3 transmissionL = 1 - fresnelSchlick(materialF0(surface), noL);
	return transmissionV * transmissionL * (1 - surface.metallic) * surface.baseColor
		* (noL * surface.occlusion / pbrPi);
}

float giAreaPdf(float solidAnglePdf, float3 source, float3 position, float3 normal)
{
	float3 delta = position - source;
	float distanceSquared = dot(delta, delta);
	if (solidAnglePdf <= 0 || distanceSquared <= 1e-12f)
	{
		return 0;
	}
	return solidAnglePdf * abs(dot(normal, delta * rsqrt(distanceSquared))) / distanceSquared;
}

float3 shadeGiCandidate(GiCandidate candidate)
{
	if (candidate.valid == 0)
	{
		return 0;
	}
	// Single candidate, M=1: normalized reservoir weight is 1 / proposal PDF.
	return candidate.diffuseBrdfCosine * candidate.radiance * candidate.inversePdf;
}

void addGiPathRadiance(float3 value, float3 throughput, bool capture, float3 suffixThroughput,
	float3 diffuseRatio, inout float3 radiance, inout float3 suffixRadiance, inout float3 referenceDiffuse)
{
	float3 contribution = throughput * value;
	radiance += contribution;
	if (capture)
	{
		suffixRadiance += suffixThroughput * value;
		referenceDiffuse += diffuseRatio * contribution;
	}
}
