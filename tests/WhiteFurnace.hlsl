#include "../samples/UsdKitchen/Pbr.hlsli"
#include "../samples/UsdKitchen/EnvironmentSampling.hlsli"
#include "../samples/UsdKitchen/RectangleLight.hlsli"

struct TestCase
{
	float3 baseColor;
	float metallic;
	float roughness;
	float specular;
	float noV;
	uint flags; // 1: cosine reference, 2: MIS, 4: finite rectangle target.
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
	RectangleLight rectangle = (RectangleLight)0;
	rectangle.center = float3(0,0,2);
	rectangle.enabled = 1;
	rectangle.axisU = float3(1,0,0);
	rectangle.axisV = float3(0,-1,0);
	rectangle.width = rectangle.height = 2;
	rectangle.radiance = 1;
	bool rectangleTarget = (input.flags & 4u) != 0;
	uint state = (group.x + 1u) * 277803737u + (thread + 1u) * 747796405u;
	float3 sum = 0, sumSquared = 0, reference = 0, referenceSquared = 0;
	uint invalid = 0, referenceInvalid = 0;
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
				weight *= powerHeuristic(pdf, lightPdf);
			}
			float lightPdf;
			float2 lightU = float2(random(state), random(state));
			float3 light;
			if (rectangleTarget)
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
