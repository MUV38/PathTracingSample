struct PrimarySurface
{
	float3 position;
	float depth;
	float3 normal;
	uint materialIndex;
	float3 geometricNormal;
	uint triangleIndex;
	float3 baseColor;
	uint valid;
	float2 motion;
	float roughness;
	float padding;
};
struct LightReservoir
{
	uint lightIndex;
	float2 uv;
	float weight;
	float weightSum;
	float target;
	float count;
	float proposalPdf;
};
static const uint environmentLightIndex = 0xffffffffu;
float2 encodeEnvironmentSample(float3 direction)
{
	return float2(atan2(direction.z, direction.x), direction.y);
}
float3 decodeEnvironmentSample(float2 encoded)
{
	float sine = sqrt(max(0.0f, 1.0f - encoded.y * encoded.y));
	return float3(sine * cos(encoded.x), encoded.y, sine * sin(encoded.x));
}
float3 evaluateEnvironmentReservoirSample(Surface surface, float3 view, float3 direction, float3 radiance, float environmentPdf)
{
	float noL = dot(surface.normal, direction);
	if (noL <= 0 || dot(surface.geometricNormal, direction) <= 0)
	{
		return 0;
	}
	float bsdfPdf;
	float3 bsdf = evaluateBsdf(surface, view, direction, bsdfPdf);
	// Resample the light-side MIS integral, retaining the complementary BSDF
	// estimator. This handles filtered HDR peaks and narrow specular lobes.
	return bsdf * noL * surface.occlusion * radiance * powerHeuristic(environmentPdf, bsdfPdf);
}
float environmentReservoirHitWeight(float environmentPdf, float bsdfPdf, bool delta)
{
	return delta ? 1.0f : powerHeuristic(bsdfPdf, environmentPdf);
}
void streamReservoir(inout LightReservoir reservoir, LightReservoir candidate, float target, float weight, float count, float u)
{
	reservoir.count += count;
	reservoir.weightSum += weight;
	if (weight > 0.0f && u * reservoir.weightSum < weight)
	{
		reservoir.lightIndex = candidate.lightIndex;
		reservoir.uv = candidate.uv;
		reservoir.target = target;
		reservoir.proposalPdf = candidate.proposalPdf;
	}
}
void finishReservoir(inout LightReservoir reservoir)
{
	reservoir.weight = reservoir.target > 0.0f && reservoir.count > 0.0f
		? reservoir.weightSum / (reservoir.count * reservoir.target) : 0.0f;
}
bool compatiblePrimarySurfaces(PrimarySurface a, PrimarySurface b)
{
	return a.valid != 0 && b.valid != 0 && a.materialIndex == b.materialIndex
		&& dot(a.normal, b.normal) > 0.95f && dot(a.geometricNormal, b.geometricNormal) > 0.95f
		&& length(a.position - b.position) < max(0.01f, a.depth * 0.02f);
}
float2 reprojectPrimary(float3 position, row_major float4x4 previousViewProjection, float2 dimensions, float previousViewportLeft)
{
	float4 clip = mul(float4(position, 1), previousViewProjection);
	if (clip.w <= 0.0f)
	{
		return -1.0f;
	}
	float2 uv = float2(clip.x, -clip.y) / clip.w * 0.5f + 0.5f;
	return uv * float2(dimensions.x - previousViewportLeft, dimensions.y) + float2(previousViewportLeft, 0);
}

float3 evaluateReservoirSample(Surface surface, float3 view, MeshLight light, float2 uv)
{
	float3 lightPoint = sampleMeshLightPoint(light, uv);
	float3 delta = lightPoint - surface.position;
	float distanceSquared = dot(delta, delta);
	if (light.area <= 0 || distanceSquared <= 1e-12f)
	{
		return 0;
	}
	float3 direction = delta * rsqrt(distanceSquared);
	float noL = dot(surface.normal, direction);
	if (noL <= 0 || dot(surface.geometricNormal, direction) <= 0)
	{
		return 0;
	}
	float lightCosine = abs(dot(cross(light.edge1, light.edge2) / (2.0f * light.area), direction));
	float unusedPdf;
	return evaluateBsdf(surface, view, direction, unusedPdf) * noL * surface.occlusion * light.radiance * lightCosine / distanceSquared;
}

float lightSampleTarget(float3 value, float areaDensity)
{
	return max(dot(value, float3(0.2126f, 0.7152f, 0.0722f)), 1e-5f * areaDensity);
}
