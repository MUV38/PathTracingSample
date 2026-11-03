#define PBR_COMPARE_NDF
#include "../samples/UsdKitchen/Pbr.hlsli"
#include "../samples/UsdKitchen/EnvironmentSampling.hlsli"
#include "../samples/UsdKitchen/RectangleLight.hlsli"
#include "../samples/UsdKitchen/MeshLight.hlsli"
#include "../samples/UsdKitchen/RussianRoulette.hlsli"
#include "../samples/UsdKitchen/RestirDI.hlsli"
#include "../samples/UsdKitchen/RestirGI.hlsli"

struct TestCase
{
	float3 baseColor;
	float metallic;
	float roughness;
	float specular;
	float noV;
	uint flags; // 1: reference, 2: MIS, 4: rectangle, 8: mesh, 16: NDF, 32: single scatter, 64: RR chain, 128: RR off.
	// 256: mesh reservoir, 512/1024: reservoir mode, 2048: mixed-light reservoir,
	// 4096: include mesh, 8192: include rectangle, 16384: exclude environment.
	// 32768: multiple rectangles, 65536: 32 slots (otherwise 2).
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
float3 testEnvironmentReservoirValue(Surface surface, float3 view, LightReservoir reservoir, float sampleRatio, RectangleLight rectangle)
{
	if (reservoir.lightIndex == environmentLightIndex)
	{
		return evaluateEnvironmentReservoirSample(surface, view, decodeEnvironmentSample(reservoir.uv), 1.0f, reservoir.proposalPdf * sampleRatio);
	}
	if (reservoir.lightIndex == rectangleLightIndex)
	{
		return evaluateRectangleReservoirSample(surface, view, rectangle, reservoir.uv);
	}
	return evaluateReservoirSample(surface, view, meshLights[reservoir.lightIndex], reservoir.uv);
}
float3 testFiniteReservoirValue(Surface surface, float3 view, LightReservoir reservoir, RectangleLight lights[32])
{
	if (isRectangleSample(reservoir.lightIndex))
	{
		return evaluateRectangleReservoirSample(surface, view, lights[rectangleSampleIndex(reservoir.lightIndex)], reservoir.uv);
	}
	return evaluateReservoirSample(surface, view, meshLights[reservoir.lightIndex], reservoir.uv);
}
[numthreads(64, 1, 1)]
void CS_Main(uint3 group : SV_GroupID, uint thread : SV_GroupIndex)
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
	// Analytic GI checks: solid-angle -> area measure, the M=1 estimator,
	// and exclusion of pure-metal / low-roughness primary surfaces.
	GiCandidate gi = (GiCandidate)0;
	gi.valid = 1;
	gi.diffuseBrdfCosine = float3(0.25f, 0.5f, 0.75f);
	gi.radiance = float3(4, 2, 1);
	gi.inversePdf = 4;
	Surface giSurface = surface;
	giSurface.metallic = 1;
	float3 metalGi = giDiffuseBrdfCosine(giSurface, view, float3(0,0,1));
	giSurface.metallic = 0;
	giSurface.roughness = 0.1f;
	float3 sharpGi = giDiffuseBrdfCosine(giSurface, view, float3(0,0,1));
	if (any(abs(shadeGiCandidate(gi) - float3(4,4,3)) > 1e-6f)
		|| abs(giAreaPdf(0.25f, 0, float3(0,0,2), float3(0,0,-1)) - 0.0625f) > 1e-6f
		|| abs(giAreaPdf(0.25f, 0, float3(0,0,2), float3(0.6f,0,-0.8f)) - 0.05f) > 1e-6f
		|| giAreaPdf(0, 0, float3(0,0,2), float3(0,0,-1)) != 0
		|| giAreaPdf(1, 0, 0, float3(0,0,1)) != 0
		|| any(metalGi != 0) || any(sharpGi != 0))
	{
		++invalid;
	}
	gi.valid = 0;
	if (any(shadeGiCandidate(gi) != 0))
	{
		++invalid;
	}
	uint rejected = 0;
	bool multipleRectangles = (input.flags & 32768u) != 0;
	uint rectangleCount = (input.flags & 65536u) != 0 ? 32u : 2u;
	RectangleLight rectangleLights[32];
	if (multipleRectangles)
	{
		float totalPower = 0;
		for (uint i = 0; i < rectangleCount; ++i)
		{
			RectangleLight light = rectangle;
			light.center.z = float(i + 2u);
			light.width = light.height = light.center.z;
			light.radiance = (i & 1u) == 0 ? float3(1,0.2f,0.1f) : float3(0.1f,0.5f,1);
			if (rectangleCount == 32 && i < 2)
			{
				light.enabled = i == 0 ? 0u : 1u;
				light.radiance = 0;
			}
			light.selectionPdf = light.width * light.height * dot(light.radiance, float3(0.2126f,0.7152f,0.0722f));
			totalPower += light.selectionPdf;
			rectangleLights[i] = light;
		}
		float cdf = 0;
		for (uint i = 0; i < rectangleCount; ++i)
		{
			float previous = cdf;
			cdf = i + 1u == rectangleCount ? 1.0f : cdf + rectangleLights[i].selectionPdf / totalPower;
			rectangleLights[i].cdf = cdf;
			rectangleLights[i].selectionPdf = cdf - previous;
		}
		float distance;
		uint nearest = nearestRectangleLight(rectangleLights, rectangleCount, 0, float3(0,0,1), 0.001f, 100, distance);
		if (nearest != (rectangleCount == 32 ? 1u : 0u) || abs(distance - (rectangleCount == 32 ? 3.0f : 2.0f)) > 0.001f
			|| nearestRectangleLight(rectangleLights, rectangleCount, float3(0,0,100), float3(0,0,-1), 0.001f, 100, distance) != rectangleCount - 1u
			|| sampleRectangleLight(rectangleLights, rectangleCount, 0) != (rectangleCount == 32 ? 2u : 0u))
		{
			++invalid;
		}
	}
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
		if ((input.flags & 2048u) != 0)
		{
			uint mode = (input.flags >> 9u) & 3u;
			bool mixed = (input.flags & 4096u) != 0;
			bool rectangleEnabled = (input.flags & 8192u) != 0;
			bool environmentEnabled = (input.flags & 16384u) == 0;
			uint typeCount = reservoirLightTypeCount(environmentEnabled, mixed, rectangleEnabled);
			float typeProbability = 1.0f / float(typeCount);
			float sampleRatio = (mode == 0 ? 1.0f : 8.0f) * typeProbability;
			uint sources = mode >= 2 ? (mode == 3 ? 5u : 2u) : 1u;
			LightReservoir combined = (LightReservoir)0;
			for (uint sourceIndex = 0; sourceIndex < sources; ++sourceIndex)
			{
				Surface sourceSurface = surface;
				if ((sourceIndex & 1u) != 0)
				{
					sourceSurface.normal = sourceSurface.geometricNormal = -surface.normal;
				}
				LightReservoir reservoir = (LightReservoir)0;
				for (uint candidateIndex = 0; candidateIndex < (mode == 0 ? 1u : 8u); ++candidateIndex)
				{
					LightReservoir candidate = (LightReservoir)0;
					float3 value;
					uint type = min(uint(random(state) * float(typeCount)), typeCount - 1u);
					if (environmentEnabled && type == 0)
					{
						candidate.lightIndex = environmentLightIndex;
						float2 lightU = float2(random(state), random(state));
						float3 direction = sampleEnvironmentDirection(environmentDistribution, uint2(16,8), lightU, 0.7f, candidate.proposalPdf);
						candidate.uv = encodeEnvironmentSample(direction);
						value = evaluateEnvironmentReservoirSample(sourceSurface, view, decodeEnvironmentSample(candidate.uv), 1.0f, candidate.proposalPdf * sampleRatio);
					}
					else
					{
						if (rectangleEnabled && type == typeCount - 1u)
						{
							candidate.lightIndex = rectangleLightIndex;
							candidate.proposalPdf = 1.0f / (rectangle.width * rectangle.height);
						}
						else
						{
							candidate.lightIndex = sampleMeshLightIndex(meshLights, 2, random(state));
							candidate.proposalPdf = meshLights[candidate.lightIndex].areaDensity;
						}
						candidate.uv = float2(random(state), random(state));
						value = testEnvironmentReservoirValue(sourceSurface, view, candidate, sampleRatio, rectangle);
					}
					float pdf = candidate.proposalPdf * typeProbability;
					float target = lightSampleTarget(value, pdf);
					streamReservoir(reservoir, candidate, target, target / pdf, 1, random(state));
				}
				finishReservoir(reservoir);
				float3 value = testEnvironmentReservoirValue(surface, view, reservoir, sampleRatio, rectangle);
				float target = lightSampleTarget(value, reservoir.proposalPdf * typeProbability);
				float count = min(reservoir.count, sourceIndex == 0 ? 8.0f : 4.0f);
				streamReservoir(combined, reservoir, target, target * reservoir.weight * count, count, random(state));
			}
			finishReservoir(combined);
			float3 value = testEnvironmentReservoirValue(surface, view, combined, sampleRatio, rectangle);
			value *= combined.weight;
			float3 direction, weight;
			float3 u = float3(random(state), random(state), random(state));
			if (sampleBsdf(surface, view, u, direction, weight))
			{
				bool delta = surface.roughness < 0.001f && u.z < specularProbability(surface, view);
				float pdf = environmentPdf(environmentDistribution, uint2(16,8), direction, 0.7f);
				float bsdfPdf;
				evaluateBsdf(surface, view, direction, bsdfPdf);
				if (environmentEnabled)
				{
					value += weight * environmentReservoirHitWeight(pdf * sampleRatio, bsdfPdf, delta);
				}
				float distance;
				if (delta && intersectRectangle(rectangle, 0.0f, direction, 0.0001f, 100000, distance))
				{
					value += weight * (float(mixed) + float(rectangleEnabled));
				}
			}
			if (any(!isfinite(value)) || any(value < 0) || !isfinite(combined.weight))
			{
				++invalid;
			}
			sum += value;
			sumSquared += value * value;
			maximum = max(maximum, maxComponent(value));
			continue;
		}
		if ((input.flags & 256u) != 0 || multipleRectangles)
		{
			uint mode = (input.flags >> 9u) & 3u;
			uint sources = mode >= 2 ? (mode == 3 ? 5u : 2u) : 1u;
			LightReservoir combined = (LightReservoir)0;
			for (uint sourceIndex = 0; sourceIndex < sources; ++sourceIndex)
			{
				Surface sourceSurface = surface;
				if ((sourceIndex & 1u) != 0)
				{
					// Deliberately disjoint BRDF horizons exercise the support floor.
					sourceSurface.normal = sourceSurface.geometricNormal = -surface.normal;
				}
				LightReservoir reservoir = (LightReservoir)0;
				for (uint candidateIndex = 0; candidateIndex < (mode == 0 ? 1u : 8u); ++candidateIndex)
				{
					LightReservoir candidate = (LightReservoir)0;
					if (multipleRectangles)
					{
						uint lightIndex = sampleRectangleLight(rectangleLights, rectangleCount, random(state));
						candidate.lightIndex = rectangleLightIndex - lightIndex;
						RectangleLight light = rectangleLights[lightIndex];
						candidate.proposalPdf = light.selectionPdf / (light.width * light.height);
					}
					else
					{
						candidate.lightIndex = sampleMeshLightIndex(meshLights, 2, random(state));
						candidate.proposalPdf = meshLights[candidate.lightIndex].areaDensity;
					}
					candidate.uv = float2(random(state), random(state));
					float target = lightSampleTarget(testFiniteReservoirValue(sourceSurface, view, candidate, rectangleLights), candidate.proposalPdf);
					streamReservoir(reservoir, candidate, target, target / candidate.proposalPdf, 1, random(state));
				}
				finishReservoir(reservoir);
				float target = lightSampleTarget(testFiniteReservoirValue(surface, view, reservoir, rectangleLights), reservoir.proposalPdf);
				// Also exercise deterministic effective-count capping on reused sources.
				float count = min(reservoir.count, sourceIndex == 0 ? 8.0f : 4.0f);
				streamReservoir(combined, reservoir, target, target * reservoir.weight * count, count, random(state));
			}
			finishReservoir(combined);
			float3 value = testFiniteReservoirValue(surface, view, combined, rectangleLights) * combined.weight;
			if (any(!isfinite(value)) || any(value < 0) || !isfinite(combined.weight) || combined.count <= 0)
			{
				++invalid;
			}
			if (index == 0)
			{
				PrimarySurface a = (PrimarySurface)0;
				a.valid = 1;
				a.depth = 1;
				a.normal = a.geometricNormal = float3(0,0,1);
				PrimarySurface b = a;
				b.position.x = 1;
				float4x4 identity = float4x4(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1);
				float2 center = reprojectPrimary(0.0f, identity, float2(1280,720), 600);
				identity[3][0] = 0.1f;
				float2 moved = reprojectPrimary(0.0f, identity, float2(1280,720), 600);
				if (!compatiblePrimarySurfaces(a, a) || compatiblePrimarySurfaces(a, b)
					|| any(abs(center - float2(940,360)) > 0.001f) || any(abs(moved - float2(974,360)) > 0.001f))
				{
					++invalid;
				}
				b = a;
				b.materialIndex = 1;
				if (compatiblePrimarySurfaces(a, b))
				{
					++invalid;
				}
				b = a;
				b.normal = -a.normal;
				if (compatiblePrimarySurfaces(a, b))
				{
					++invalid;
				}
				identity[2][3] = 1;
				identity[3][3] = 0;
				if (any(reprojectPrimary(float3(0,0,-1), identity, float2(1280,720), 600) != -1.0f))
				{
					++invalid;
				}
			}
			sum += value;
			sumSquared += value * value;
			maximum = max(maximum, maxComponent(value));
			continue;
		}
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
