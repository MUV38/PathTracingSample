#include "../samples/UsdKitchen/Pbr.hlsli"
#include "../samples/UsdKitchen/EnvironmentSampling.hlsli"

struct TestCase
{
	float3 baseColor;
	float metallic;
	float roughness;
	float specular;
	float noV;
	uint referenceEnabled;
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
	float padding;
};
StructuredBuffer<TestCase> cases : register(t0);
StructuredBuffer<float2> environmentDistribution : register(t1);
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
	float3 view = float3(sqrt(1.0f - input.noV * input.noV), 0, input.noV);
	uint state = (group.x + 1u) * 277803737u + (thread + 1u) * 747796405u;
	float3 sum = 0, sumSquared = 0, reference = 0, referenceSquared = 0;
	uint invalid = 0, referenceInvalid = 0;
	float maximum = 0;
	for (uint index = 0; index < samplesPerThread; ++index)
	{
		float3 direction, weight;
		float3 u = float3(random(state), random(state), random(state));
		bool accepted = sampleBsdf(surface, view, u, direction, weight);
		if (any(!isfinite(weight)) || any(weight < 0.0f)
			|| (accepted && (any(!isfinite(direction)) || abs(dot(direction, direction) - 1.0f) > 0.001f)))
		{
			++invalid;
			weight = 0;
		}
		if (!accepted)
		{
			weight = 0;
		}
		if ((input.referenceEnabled & 2u) != 0)
		{
			bool delta = surface.roughness < 0.001f && u.z < specularProbability(surface, view);
			if (accepted && !delta)
			{
				float pdf;
				evaluateBsdf(surface, view, direction, pdf);
				weight *= powerHeuristic(pdf, environmentPdf(environmentDistribution, uint2(16,8), direction, 0.7f));
			}
			float lightPdf;
			float2 lightU = float2(random(state), random(state));
			float3 light = sampleEnvironmentDirection(environmentDistribution, uint2(16,8), lightU, 0.7f, lightPdf);
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
		if ((input.referenceEnabled & 1u) != 0)
		{
			// Independently integrate BRDF evaluation with cosine hemisphere samples.
			float radius = sqrt(random(state));
			float phi = 2.0f * pbrPi * random(state);
			float3 light = float3(radius * cos(phi), radius * sin(phi), sqrt(1.0f - radius * radius));
			float unusedPdf;
			float3 value = evaluateBsdf(surface, view, light, unusedPdf) * pbrPi;
			if (surface.roughness < 0.001f)
			{
				value += fresnelSchlick(materialF0(surface), input.noV);
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
		result.padding = 0;
		results[group.x] = result;
	}
}
