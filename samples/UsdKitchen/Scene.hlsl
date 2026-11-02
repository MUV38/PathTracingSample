#include "Pbr.hlsli"
#include "EnvironmentSampling.hlsli"

RaytracingAccelerationStructure scene : register(t0);
struct Vertex
{
	float3 position;
	float3 normal;
	float3 color;
	uint materialIndex;
};
StructuredBuffer<Vertex> vertices : register(t1);
StructuredBuffer<uint> meshletVertices : register(t2);
StructuredBuffer<uint4> meshlets : register(t3);
ByteAddressBuffer meshletTriangles : register(t4);
Texture2D<float4> environmentMap : register(t5);
StructuredBuffer<Material> materials : register(t6);
StructuredBuffer<float2> environmentDistribution : register(t7);
SamplerState environmentSampler : register(s0);
RWTexture2D<float4> outputImage : register(u0);
RWTexture2D<float4> accumulation : register(u1);
cbuffer SceneConstants : register(b0)
{
	float3 cameraPosition;
	float tanHalfFov;
	float3 cameraRight;
	float aspectRatio;
	float3 cameraUp;
	float viewportLeft;
	float3 cameraForward;
	float meshletColors;
	float3 backgroundColor;
	float exposure;
	float3 lightDirection;
	float shadows;
	float3 lightPosition;
	float pointLight;
	float3 lightColor;
	float lightStrength;
	float ambient;
	float shadowBias;
	float2 padding;
	uint frameIndex;
	uint maxBounces;
	uint samplesPerFrame;
	float environmentIntensity;
	float environmentRotation;
	uint environmentMis;
	float2 padding2;
};
struct Payload
{
	float3 color;
	float3 position;
	float3 normal;
	float3 geometricNormal;
	uint hit;
	uint materialIndex;
};
static const float pi = 3.14159265359f;

uint localIndex(uint offset)
{
	return (meshletTriangles.Load(offset & ~3u) >> ((offset & 3u) * 8u)) & 255u;
}
float random(inout uint state)
{
	state = state * 747796405u + 2891336453u;
	uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
	word = (word >> 22u) ^ word;
	return float(word >> 8u) * (1.0f / 16777216.0f);
}
float3 sampleEnvironment(float3 direction)
{
	float2 uv = environmentUv(direction, environmentRotation);
	return environmentMap.SampleLevel(environmentSampler, uv, 0).rgb * environmentIntensity;
}
float3 displayColor(float3 radiance)
{
	float3 mapped = max(radiance * exposure, 0.0f);
	mapped = mapped / (1.0f + mapped);
	return select(mapped <= 0.0031308f, mapped * 12.92f, 1.055f * pow(mapped, 1.0f / 2.4f) - 0.055f);
}
float rayBias(float3 position)
{
	return max(shadowBias, 0.000001f * max(max(abs(position.x), abs(position.y)), abs(position.z)));
}
float3 offsetOrigin(float3 position, float3 geometricNormal)
{
	return position + geometricNormal * rayBias(position);
}
float visibility(float3 position, float3 geometricNormal)
{
	if (shadows < 0.5f)
	{
		return 1.0f;
	}
	RayDesc ray;
	ray.Origin = offsetOrigin(position, geometricNormal);
	ray.TMin = 0.0f;
	ray.Direction = lightDirection;
	ray.TMax = 100000.0f;
	if (pointLight > 0.5f)
	{
		float3 delta = lightPosition - ray.Origin;
		float distance = length(delta);
		if (distance <= rayBias(position))
		{
			return 1.0f;
		}
		ray.Direction = delta / distance;
		ray.TMax = distance * 0.999999f;
	}
	RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
	query.TraceRayInline(scene, RAY_FLAG_NONE, 0xff, ray);
	query.Proceed();
	return query.CommittedStatus() == COMMITTED_NOTHING ? 1.0f : 0.0f;
}
float3 directLighting(Surface surface, float3 view)
{
	float3 toLight = lightDirection;
	float attenuation = 1.0f;
	if (pointLight > 0.5f)
	{
		float3 delta = lightPosition - surface.position;
		float distanceSquared = max(dot(delta, delta), 0.0001f);
		toLight = delta * rsqrt(distanceSquared);
		attenuation = rcp(distanceSquared);
	}
	float cosine = saturate(dot(surface.normal, toLight));
	float visible = 0.0f;
	if (cosine > 0.0f && dot(surface.geometricNormal, toLight) > 0.0f && lightStrength > 0.0f)
	{
		visible = visibility(surface.position, surface.geometricNormal);
	}
	float pdf;
	float3 bsdf = evaluateBsdf(surface, view, toLight, pdf);
	float3 diffuseFill = surface.baseColor * (1.0f - surface.metallic) * (1.0f - materialF0(surface)) / pi;
	return bsdf * lightColor * lightStrength * attenuation * cosine * visible + diffuseFill * ambient * surface.occlusion;
}
float3 environmentLighting(Surface surface, float3 view, float2 u)
{
	uint width, height;
	environmentMap.GetDimensions(width, height);
	float pdf;
	float3 direction = sampleEnvironmentDirection(environmentDistribution, uint2(width, height), u, environmentRotation, pdf);
	float cosine = dot(surface.normal, direction);
	if (pdf <= 0.0f || cosine <= 0.0f || dot(surface.geometricNormal, direction) <= 0.0f)
	{
		return 0.0f;
	}
	float bsdfPdf;
	float3 bsdf = evaluateBsdf(surface, view, direction, bsdfPdf);
	if (maxComponent(bsdf) <= 0.0f)
	{
		return 0.0f;
	}
	RayDesc ray;
	ray.Origin = offsetOrigin(surface.position, surface.geometricNormal);
	ray.Direction = direction;
	ray.TMin = 0.0f;
	ray.TMax = 100000.0f;
	// IBL occlusion is always physical; the Direct lighting shadow toggle is separate.
	RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
	query.TraceRayInline(scene, RAY_FLAG_NONE, 0xff, ray);
	query.Proceed();
	if (query.CommittedStatus() != COMMITTED_NOTHING)
	{
		return 0.0f;
	}
	return bsdf * cosine * surface.occlusion * sampleEnvironment(direction) * powerHeuristic(pdf, bsdfPdf) / pdf;
}
[shader("raygeneration")]
void RGSMain()
{
	uint2 pixel = DispatchRaysIndex().xy;
	if (float(pixel.x) < viewportLeft)
	{
		outputImage[pixel] = float4(backgroundColor, 1.0f);
		return;
	}
	uint state = pixel.x + pixel.y * DispatchRaysDimensions().x;
	state ^= (frameIndex + 1u) * 277803737u;
	float3 frameRadiance = 0.0f;
	for (uint sampleIndex = 0; sampleIndex < samplesPerFrame; ++sampleIndex)
	{
		float2 dimensions = float2(DispatchRaysDimensions().xy);
		float2 jitter = float2(random(state), random(state));
		float2 uv = float2((float(pixel.x) - viewportLeft + jitter.x) / max(dimensions.x - viewportLeft, 1.0f),
			(float(pixel.y) + jitter.y) / dimensions.y);
		float2 screen = uv * 2.0f - 1.0f;
		RayDesc ray;
		ray.Origin = cameraPosition;
		ray.Direction = normalize(cameraForward + screen.x * aspectRatio * tanHalfFov * cameraRight - screen.y * tanHalfFov * cameraUp);
		ray.TMin = 0.0001f;
		ray.TMax = 100000.0f;
		float3 throughput = 1.0f;
		float3 radiance = 0.0f;
		float missWeight = 1.0f;
		for (uint depth = 0; depth <= maxBounces; ++depth)
		{
			Payload payload;
			payload.hit = 0;
			TraceRay(scene, RAY_FLAG_NONE, 0xff, 0, 0, 0, ray, payload);
			if (payload.hit == 0)
			{
				radiance += throughput * payload.color * missWeight;
				break;
			}
			Material material = materials[payload.materialIndex];
			Surface surface;
			surface.position = payload.position;
			surface.normal = payload.normal;
			surface.geometricNormal = payload.geometricNormal;
			surface.baseColor = saturate(payload.color * material.baseColor);
			surface.metallic = material.metallic;
			surface.roughness = material.roughness;
			surface.specular = material.specular;
			surface.occlusion = material.occlusion;
			if (meshletColors > 0.5f)
			{
				surface.baseColor = payload.color;
				surface.metallic = 0;
				surface.roughness = 1;
				surface.specular = 0;
				surface.occlusion = 1;
				material.emissiveStrength = 0;
			}
			float3 view = -ray.Direction;
			radiance += throughput * (material.emissiveColor * material.emissiveStrength + directLighting(surface, view));
			if (depth == maxBounces)
			{
				break;
			}
			if (environmentMis != 0 && environmentIntensity > 0.0f)
			{
				float2 lightU = float2(random(state), random(state));
				radiance += throughput * environmentLighting(surface, view, lightU);
			}
			float3 nextDirection, weight;
			float3 u = float3(random(state), random(state), random(state));
			if (!sampleBsdf(surface, view, u, nextDirection, weight))
			{
				break;
			}
			missWeight = 1.0f;
			bool delta = surface.roughness < 0.001f && u.z < specularProbability(surface, view);
			if (environmentMis != 0 && environmentIntensity > 0.0f && !delta)
			{
				float bsdfPdf;
				evaluateBsdf(surface, view, nextDirection, bsdfPdf);
				uint width, height;
				environmentMap.GetDimensions(width, height);
				float lightPdf = environmentPdf(environmentDistribution, uint2(width, height), nextDirection, environmentRotation);
				missWeight = powerHeuristic(bsdfPdf, lightPdf);
			}
			ray.Direction = nextDirection;
			throughput *= weight;
			ray.Origin = offsetOrigin(payload.position, payload.geometricNormal);
			ray.TMin = 0.0f;
		}
		frameRadiance += radiance;
	}
	frameRadiance /= float(samplesPerFrame);
	float3 average = frameRadiance;
	if (frameIndex > 0)
	{
		average = lerp(accumulation[pixel].rgb, frameRadiance, 1.0f / (float(frameIndex) + 1.0f));
	}
	accumulation[pixel] = float4(average, 1.0f);
	outputImage[pixel] = float4(displayColor(average), 1.0f);
}
[shader("miss")]
void RMSMain(inout Payload payload)
{
	payload.hit = 0;
	payload.color = sampleEnvironment(WorldRayDirection());
}
[shader("closesthit")]
void CHSMain(inout Payload payload, in BuiltInTriangleIntersectionAttributes attributes)
{
	uint meshletIndex = GeometryIndex();
	uint first = meshlets[meshletIndex].y + PrimitiveIndex() * 3;
	uint vertexOffset = meshlets[meshletIndex].x;
	Vertex a = vertices[meshletVertices[vertexOffset + localIndex(first)]];
	Vertex b = vertices[meshletVertices[vertexOffset + localIndex(first + 1)]];
	Vertex c = vertices[meshletVertices[vertexOffset + localIndex(first + 2)]];
	float3 weights = float3(1.0f - attributes.barycentrics.x - attributes.barycentrics.y, attributes.barycentrics);
	float3 geometricNormal = normalize(cross(b.position - a.position, c.position - a.position));
	if (dot(geometricNormal, WorldRayDirection()) > 0.0f)
	{
		geometricNormal = -geometricNormal;
	}
	float3 normal = a.normal * weights.x + b.normal * weights.y + c.normal * weights.z;
	if (dot(normal, normal) < 0.000001f)
	{
		normal = geometricNormal;
	}
	normal = normalize(normal);
	if (dot(normal, geometricNormal) < 0.0f)
	{
		normal = -normal;
	}
	if (dot(normal, -WorldRayDirection()) <= 0.0001f)
	{
		normal = geometricNormal;
	}
	float3 color = a.color * weights.x + b.color * weights.y + c.color * weights.z;
	if (meshletColors > 0.5f)
	{
		uint hash = (meshletIndex + 1) * 747796405u + 2891336453u;
		hash = ((hash >> ((hash >> 28u) + 4u)) ^ hash) * 277803737u;
		hash = (hash >> 22u) ^ hash;
		color = 0.2f + 0.8f * float3(hash & 255u, (hash >> 8u) & 255u, (hash >> 16u) & 255u) / 255.0f;
	}
	payload.position = a.position + attributes.barycentrics.x * (b.position - a.position)
		+ attributes.barycentrics.y * (c.position - a.position);
	payload.normal = normal;
	payload.geometricNormal = geometricNormal;
	payload.color = color;
	payload.hit = 1;
	payload.materialIndex = a.materialIndex;
}
