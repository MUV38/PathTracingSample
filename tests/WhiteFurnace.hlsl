#define PBR_COMPARE_NDF
#include "../samples/UsdKitchen/Pbr.hlsli"
#include "../samples/UsdKitchen/EnvironmentSampling.hlsli"
#include "../samples/UsdKitchen/RectangleLight.hlsli"
#include "../samples/UsdKitchen/MeshLight.hlsli"
#include "../samples/UsdKitchen/RussianRoulette.hlsli"

struct TestCase
{
	float3 baseColor;
	float metallic;
	float roughness;
	float specular;
	float noV;
	uint flags; // 1: reference, 2: MIS, 4: rectangle, 8: mesh, 16: NDF, 32: single scatter, 64: RR chain, 128: RR off.
};
struct Result
{
	float3 mean;
	uint invalid;
	float3 variance;
	float maxWeight;
	float3 reference;
	uint referenceInvalid;
	float3 referenceVariance;
	float rejectionRate;
};
StructuredBuffer<TestCase> cases : register(t0);
StructuredBuffer<float2> environmentDistribution : register(t1);
StructuredBuffer<MeshLight> meshLights : register(t2);
RWStructuredBuffer<Result> results : register(u0);
cbuffer Settings : register(b0)
{
	uint samplesPerThread;
};
groupshared float3 sums[64];
groupshared float3 squares[64];
groupshared float3 referenceSums[64];
groupshared float3 referenceSquares[64];
groupshared uint errors[64];
groupshared uint referenceErrors[64];
groupshared float maxima[64];
groupshared uint rejections[64];

float random(inout uint state)
{
	state = state * 747796405u + 2891336453u;
	uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
	return float(((word >> 22u) ^ word) >> 8u) / 16777216.0f;
}
[numthreads(64, 1, 1)]
void CSMain(uint3 group : SV_GroupID, uint thread : SV_GroupIndex)
{
	TestCase input = cases[group.x];
	Surface surface = (Surface)0;
	surface.normal = surface.geometricNormal = float3(0,0,1);
	surface.baseColor = input.baseColor;
	surface.metallic = input.metallic;
	surface.roughness = input.roughness;
	surface.specular = input.specular;
	surface.occlusion = 1;
	surface.useNdf = (input.flags & 16u) != 0;
	surface.singleScatterOnly = (input.flags & 32u) != 0;
	float3 view = float3(sqrt(1.0f - input.noV * input.noV), 0, input.noV);
	RectangleLight rectangle = (RectangleLight)0;
	rectangle.center = float3(0,0,2);
	rectangle.enabled = 1;
	rectangle.axisU = float3(1,0,0);
	rectangle.axisV = float3(0,-1,0);
	rectangle.width = rectangle.height = 2;
	rectangle.radiance = 1;
	bool meshTarget = (input.flags & 8u) != 0;
	bool rectangleTarget = (input.flags & 4u) != 0;
	uint state = (group.x + 1u) * 277803737u + (thread + 1u) * 747796405u;
	float3 sum = 0, sumSquared = 0, reference = 0, referenceSquared = 0;
	uint invalid = 0, referenceInvalid = 0;
	uint rejected = 0;
	if (rectangleTarget)
	{
		float distance;
		if (!intersectRectangle(rectangle, 0.0f, float3(0,0,1), 0.0001f, 10, distance) || abs(distance - 2) > 0.00001f
			|| intersectRectangle(rectangle, 0.0f, float3(0,0,1), 0.0001f, 1, distance)
			|| intersectRectangle(rectangle, 0.0f, float3(1,0,0), 0.0001f, 10, distance)
			|| intersectRectangle(rectangle, float3(2,0,0), float3(0,0,1), 0.0001f, 10, distance)
			|| abs(rectanglePdf(rectangle, 0.0f, rectangle.center) - 1.0f) > 0.00001f
			|| rectanglePdf(rectangle, float3(0,0,3), rectangle.center) != 0.0f
			|| any(rectangleEmission(rectangle, float3(0,0,-1)) != 0.0f))
		{
			++invalid;
		}
	}
	float maximum = 0;
	for (uint index = 0; index < samplesPerThread; ++index)
	{
		if ((input.flags & 64u) != 0)
		{
			// An eight-vertex path with known geometric-series radiance. This
			// uses the production roulette helper and also counts visited vertices.
			float3 throughput = 1, value = 0;
			uint visited = 0;
			for (uint bounce = 0; bounce < 8; ++bounce)
			{
				++visited;
				value += throughput;
				if (bounce == 7)
				{
					break;
				}
				throughput *= input.baseColor;
				float probability = pathSurvivalProbability(throughput, bounce + 1u, (input.flags & 128u) == 0);
				if (probability < 1.0f)
				{
					if (!surviveRoulette(throughput, probability, random(state)))
					{
						++rejected;
						break;
					}
				}
			}
			if (any(!isfinite(value)) || any(value < 0))
			{
				++invalid;
			}
			sum += value;
			sumSquared += value * value;
			reference += float(visited);
			referenceSquared += float(visited * visited);
			maximum = max(maximum, maxComponent(value));
			if (index == 0)
			{
				float3 probe = 0.25f;
				if (pathSurvivalProbability(probe, 2, true) != 1.0f
					|| pathSurvivalProbability(probe, 3, true) != 0.25f
					|| pathSurvivalProbability(0.0001f.xxx, 3, true) != 0.05f
					|| pathSurvivalProbability(2.0f.xxx, 3, true) != 1.0f
					|| pathSurvivalProbability(0.0f.xxx, 0, false) != 0.0f
					|| surviveRoulette(probe, 0.25f, 0.25f)
					|| !surviveRoulette(probe, 0.25f, 0.0f) || any(probe != 1.0f))
				{
					++invalid;
				}
			}
			continue;
		}
		float3 direction, weight;
		float3 u = float3(random(state), random(state), random(state));
		if (index == 0)
		{
			float3 probe = normalize(float3(0.3f, 0.4f, 0.5f));
			float unusedPdf;
			float3 forward = evaluateBsdf(surface, view, probe, unusedPdf);
			float3 backward = evaluateBsdf(surface, probe, view, unusedPdf);
			if (any(abs(forward - backward) > 0.0001f * max(1.0f, abs(forward))))
			{
				++invalid;
			}
		}
		bool accepted = sampleBsdf(surface, view, u, direction, weight);
		if (any(!isfinite(weight)) || any(weight < 0.0f)
			|| (accepted && (any(!isfinite(direction)) || abs(dot(direction, direction) - 1.0f) > 0.001f)))
		{
			++invalid;
			weight = 0;
		}
		if (!accepted)
		{
			++rejected;
			weight = 0;
		}
		float hitDistance = 0;
		if (rectangleTarget && accepted && !intersectRectangle(rectangle, 0.0f, direction, 0.0001f, 100000.0f, hitDistance))
		{
			weight = 0;
		}
		if ((input.flags & 2u) != 0)
		{
			bool delta = surface.roughness < 0.001f && u.z < specularProbability(surface, view);
			if (accepted && !delta)
			{
				float pdf;
				evaluateBsdf(surface, view, direction, pdf);
				float lightPdf = rectangleTarget ? rectanglePdf(rectangle, 0.0f, direction * hitDistance)
					: environmentPdf(environmentDistribution, uint2(16,8), direction, 0.7f);
				if (meshTarget)
				{
					uint lightTriangle = direction.x >= direction.y ? 0u : 1u;
					lightPdf = meshLightPdf(meshLights[lightTriangle], 0.0f, direction * hitDistance);
				}
				weight *= powerHeuristic(pdf, lightPdf);
			}
			float lightPdf;
			float2 lightU = float2(random(state), random(state));
			float3 light;
			if (meshTarget)
			{
				MeshLight lightTriangle = meshLights[sampleMeshLightIndex(meshLights, 2, random(state))];
				float3 lightPoint = sampleMeshLightPoint(lightTriangle, lightU);
				light = normalize(lightPoint);
				lightPdf = meshLightPdf(lightTriangle, 0.0f, lightPoint);
			}
			else if (rectangleTarget)
			{
				float3 lightPoint = sampleRectanglePoint(rectangle, lightU);
				light = normalize(lightPoint);
				lightPdf = rectanglePdf(rectangle, 0.0f, lightPoint);
			}
			else
			{
				light = sampleEnvironmentDirection(environmentDistribution, uint2(16,8), lightU, 0.7f, lightPdf);
			}
			float bsdfPdf;
			float3 bsdf = evaluateBsdf(surface, view, light, bsdfPdf);
			weight += bsdf * max(light.z, 0.0f) * powerHeuristic(lightPdf, bsdfPdf) / lightPdf;
			if (any(!isfinite(weight)) || any(weight < 0.0f))
			{
				++invalid;
				weight = 0;
			}
		}
		sum += weight;
		sumSquared += weight * weight;
		maximum = max(maximum, maxComponent(weight));
		if ((input.flags & 1u) != 0)
		{
			// Independently integrate BRDF evaluation with cosine hemisphere samples.
			float radius = sqrt(random(state));
			float phi = 2.0f * pbrPi * random(state);
			float3 light = float3(radius * cos(phi), radius * sin(phi), sqrt(1.0f - radius * radius));
			float unusedPdf;
			float3 value = evaluateBsdf(surface, view, light, unusedPdf) * pbrPi;
			float referenceDistance;
			if (rectangleTarget && !intersectRectangle(rectangle, 0.0f, light, 0.0001f, 100000.0f, referenceDistance))
			{
				value = 0;
			}
			if (surface.roughness < 0.001f)
			{
				float mirrorDistance;
				float3 mirrorDirection = reflect(-view, surface.normal);
				if (!rectangleTarget || intersectRectangle(rectangle, 0.0f, mirrorDirection, 0.0001f, 100000.0f, mirrorDistance))
				{
					value += fresnelSchlick(materialF0(surface), input.noV);
				}
			}
			if (any(!isfinite(value)) || any(value < 0.0f))
			{
				++referenceInvalid;
				value = 0;
			}
			reference += value;
			referenceSquared += value * value;
		}
	}
	sums[thread] = sum;
	squares[thread] = sumSquared;
	referenceSums[thread] = reference;
	referenceSquares[thread] = referenceSquared;
	errors[thread] = invalid;
	referenceErrors[thread] = referenceInvalid;
	maxima[thread] = maximum;
	rejections[thread] = rejected;
	GroupMemoryBarrierWithGroupSync();
	for (uint stride = 32; stride > 0; stride /= 2)
	{
		if (thread < stride)
		{
			sums[thread] += sums[thread + stride];
			squares[thread] += squares[thread + stride];
			referenceSums[thread] += referenceSums[thread + stride];
			referenceSquares[thread] += referenceSquares[thread + stride];
			errors[thread] += errors[thread + stride];
			referenceErrors[thread] += referenceErrors[thread + stride];
			maxima[thread] = max(maxima[thread], maxima[thread + stride]);
			rejections[thread] += rejections[thread + stride];
		}
		GroupMemoryBarrierWithGroupSync();
	}
	if (thread == 0)
	{
		float count = float(samplesPerThread * 64);
		Result result;
		result.mean = sums[0] / count;
		result.variance = max(0.0f, squares[0] / count - result.mean * result.mean);
		result.reference = referenceSums[0] / count;
		result.referenceVariance = max(0.0f, referenceSquares[0] / count - result.reference * result.reference);
		result.invalid = errors[0];
		result.referenceInvalid = referenceErrors[0];
		result.maxWeight = maxima[0];
		result.rejectionRate = float(rejections[0]) / count;
		results[group.x] = result;
	}
}
