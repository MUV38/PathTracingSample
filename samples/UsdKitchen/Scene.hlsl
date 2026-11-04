#include "Pbr.hlsli"
#include "EnvironmentSampling.hlsli"
#include "RectangleLight.hlsli"
#include "MeshLight.hlsli"
#include "RussianRoulette.hlsli"
#include "RestirDI.hlsli"
#include "RestirGI.hlsli"

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
StructuredBuffer<MeshLight> meshLights : register(t8);
SamplerState environmentSampler : register(s0);
RWTexture2D<float4> outputImage : register(u0);
RWTexture2D<float4> accumulation : register(u1);
RWStructuredBuffer<PrimarySurface> primarySurfaces : register(u2);
RWStructuredBuffer<PrimarySurface> previousSurfaces : register(u3);
RWStructuredBuffer<LightReservoir> initialReservoirs : register(u4);
RWStructuredBuffer<LightReservoir> currentReservoirs : register(u5);
RWStructuredBuffer<LightReservoir> previousReservoirs : register(u6);
RWTexture2D<float4> rrColor : register(u7);
RWTexture2D<float> rrDepth : register(u8);
RWTexture2D<float2> rrMotion : register(u9);
RWTexture2D<float4> rrNormalRoughness : register(u10);
RWTexture2D<float4> rrDiffuseAlbedo : register(u11);
RWTexture2D<float4> rrSpecularAlbedo : register(u12);
RWTexture2D<float> rrSpecularDistance : register(u13);
RWTexture2D<float4> rrOutput : register(u14);
RWStructuredBuffer<GiCandidate> giCandidates : register(u15);
RWStructuredBuffer<GiCandidate> previousGiCandidates : register(u16);
cbuffer RestirPass : register(b1)
{
	uint restirPass;
};
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
	uint pathReplay;
	float padding2;
	uint rectangleCount;
	uint sampledRectangleCount;
	float2 rectanglePadding;
	RectangleLight rectangles[32];
	uint meshLightCount;
	uint emissiveMis;
	uint multipleScattering;
	uint russianRoulette;
	uint restirMode;
	uint restirDebug;
	uint rrEnabled;
	uint rrDebug;
	uint giEnabled;
	uint giView;
	uint giUseHistory;
	uint giUseSpatial;
	uint historyValid;
	uint restirFrame;
	uint giHistoryValid;
	float historyPadding;
	row_major float4x4 previousViewProjection;
	float previousViewportLeft;
	float3 previousCameraPadding;
	float2 rrJitter;
	uint rrReset;
	uint rrFrame;
};
struct Payload
{
	float3 color;
	float3 position;
	float3 normal;
	float3 geometricNormal;
	uint hit;
	uint materialIndex;
	uint triangleIndex;
	uint flatNormal;
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
uint nearestRectangle(RayDesc ray, out float distance)
{
	return nearestRectangleLight(rectangles, rectangleCount, ray.Origin, ray.Direction, ray.TMin, ray.TMax, distance);
}
uint sampleRectangleIndex(float u)
{
	return sampleRectangleLight(rectangles, rectangleCount, u);
}
bool rectangleOccludes(RayDesc ray)
{
	float distance;
	return nearestRectangle(ray, distance) < rectangleCount;
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
	if (rectangleOccludes(ray))
	{
		return 0.0f;
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
bool visibleToEnvironment(Surface surface, float3 direction)
{
	RayDesc ray;
	ray.Origin = offsetOrigin(surface.position, surface.geometricNormal);
	ray.Direction = direction;
	ray.TMin = 0.0f;
	ray.TMax = 100000.0f;
	// IBL occlusion is always physical; the Direct lighting shadow toggle is separate.
	if (rectangleOccludes(ray))
	{
		return false;
	}
	RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
	query.TraceRayInline(scene, RAY_FLAG_NONE, 0xff, ray);
	query.Proceed();
	if (query.CommittedStatus() != COMMITTED_NOTHING)
	{
		return false;
	}
	return true;
}
float3 environmentLighting(Surface surface, float3 view, float2 u, out float proposalPdf)
{
	uint width, height;
	environmentMap.GetDimensions(width, height);
	float3 direction = sampleEnvironmentDirection(environmentDistribution, uint2(width, height), u, environmentRotation, proposalPdf);
	float cosine = dot(surface.normal, direction);
	if (proposalPdf <= 0.0f || cosine <= 0.0f || dot(surface.geometricNormal, direction) <= 0.0f)
	{
		return 0.0f;
	}
	float bsdfPdf;
	float3 bsdf = evaluateBsdf(surface, view, direction, bsdfPdf);
	if (maxComponent(bsdf) <= 0.0f)
	{
		return 0.0f;
	}
	if (!visibleToEnvironment(surface, direction))
	{
		return 0.0f;
	}
	return bsdf * cosine * surface.occlusion * sampleEnvironment(direction) * powerHeuristic(proposalPdf, bsdfPdf) / proposalPdf;
}
bool visibleToPoint(Surface surface, float3 lightPoint)
{
	RayDesc ray;
	ray.Origin = offsetOrigin(surface.position, surface.geometricNormal);
	float3 shadowDelta = lightPoint - ray.Origin;
	float distance = length(shadowDelta);
	ray.Direction = shadowDelta / max(distance, 0.000001f);
	ray.TMin = 0.0f;
	ray.TMax = max(0.0f, distance - rayBias(lightPoint));
	if (rectangleOccludes(ray))
	{
		return false;
	}
	RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
	query.TraceRayInline(scene, RAY_FLAG_NONE, 0xff, ray);
	query.Proceed();
	if (query.CommittedStatus() != COMMITTED_NOTHING)
	{
		return false;
	}
	return true;
}
float3 rectangleLighting(Surface surface, float3 view, RectangleLight rectangle, float2 u, out float proposalPdf)
{
	float3 lightPoint = sampleRectanglePoint(rectangle, u);
	float3 delta = lightPoint - surface.position;
	float distanceSquared = dot(delta, delta);
	proposalPdf = rectangle.selectionPdf * rectanglePdf(rectangle, surface.position, lightPoint);
	if (proposalPdf <= 0.0f)
	{
		return 0.0f;
	}
	float3 direction = delta * rsqrt(distanceSquared);
	float cosine = dot(surface.normal, direction);
	if (cosine <= 0.0f || dot(surface.geometricNormal, direction) <= 0.0f)
	{
		return 0.0f;
	}
	float bsdfPdf;
	float3 bsdf = evaluateBsdf(surface, view, direction, bsdfPdf);
	if (maxComponent(bsdf) <= 0.0f)
	{
		return 0.0f;
	}
	if (!visibleToPoint(surface, lightPoint))
	{
		return 0.0f;
	}
	return bsdf * cosine * surface.occlusion * rectangle.radiance * powerHeuristic(proposalPdf, bsdfPdf) / proposalPdf;
}
float3 emissiveLighting(Surface surface, float3 view, float3 u, uint hitTriangle, out float proposalPdf)
{
	proposalPdf = 0;
	uint index = sampleMeshLightIndex(meshLights, meshLightCount, u.z);
	if (index == hitTriangle)
	{
		return 0.0f;
	}
	MeshLight light = meshLights[index];
	float3 lightPoint = sampleMeshLightPoint(light, u.xy);
	proposalPdf = meshLightPdf(light, surface.position, lightPoint);
	if (proposalPdf <= 0.0f)
	{
		return 0.0f;
	}
	float3 direction = normalize(lightPoint - surface.position);
	float cosine = dot(surface.normal, direction);
	if (cosine <= 0.0f || dot(surface.geometricNormal, direction) <= 0.0f)
	{
		return 0.0f;
	}
	float bsdfPdf;
	float3 bsdf = evaluateBsdf(surface, view, direction, bsdfPdf);
	if (maxComponent(bsdf) <= 0.0f || !visibleToPoint(surface, lightPoint))
	{
		return 0.0f;
	}
	return bsdf * cosine * surface.occlusion * light.radiance * powerHeuristic(proposalPdf, bsdfPdf) / proposalPdf;
}
#include "RestirPasses.hlsli"
Surface pathSurface(Payload payload, Material material)
{
	Surface surface;
	surface.singleScatterOnly = multipleScattering == 0;
	surface.position = payload.position;
	surface.normal = payload.normal;
	surface.geometricNormal = payload.geometricNormal;
	surface.baseColor = saturate(payload.color * material.baseColor);
	surface.metallic = material.metallic;
	surface.roughness = material.roughness;
	surface.specular = material.specular;
	surface.occlusion = material.occlusion;
	return surface;
}
#include "GiTemporal.hlsli"
#include "GiSpatial.hlsli"
#include "RayReconstruction.hlsli"
void recordPathValue(inout uint digest, uint value)
{
	digest = (digest ^ value) * 16777619u;
}

void recordPathVector(inout uint digest, float3 value)
{
	recordPathValue(digest, asuint(value.x));
	recordPathValue(digest, asuint(value.y));
	recordPathValue(digest, asuint(value.z));
}

[shader("raygeneration")]
void RGS_Main()
{
	uint2 pixel = DispatchRaysIndex().xy;
	uint state = pixel.x + pixel.y * DispatchRaysDimensions().x;
	bool fixedGiGuide = giView >= 4 && giView <= 6;
	state ^= ((fixedGiGuide ? 0u : (giEnabled >= 2 ? restirFrame : (rrEnabled != 0 ? rrFrame : frameIndex))) + 1u) * 277803737u;
	if (restirPass == 3)
	{
		if (float(pixel.x) < viewportLeft)
		{
			outputImage[pixel] = float4(backgroundColor, 1);
		}
		else
		{
			uint2 guidePixel = uint2(pixel.x - uint(viewportLeft), pixel.y);
			float3 color = 0;
			if (rrDebug == 0)
			{
				color = displayColor(rrOutput[guidePixel].rgb);
			}
			else if (rrDebug == 1)
			{
				color = displayColor(rrColor[guidePixel].rgb);
			}
			else if (rrDebug == 2)
			{
				color = rrNormalRoughness[guidePixel].xyz * 0.5f + 0.5f;
			}
			else if (rrDebug == 3)
			{
				color = rrNormalRoughness[guidePixel].www;
			}
			else if (rrDebug == 4 || rrDebug == 5)
			{
				float3 albedo = rrDebug == 4 ? rrDiffuseAlbedo[guidePixel].rgb : rrSpecularAlbedo[guidePixel].rgb;
				albedo = saturate(albedo);
				color = select(albedo <= 0.0031308f, albedo * 12.92f, 1.055f * pow(albedo, 1.0f / 2.4f) - 0.055f);
			}
			else if (rrDebug == 6)
			{
				color = float3(0.5f + rrMotion[guidePixel] * 0.05f, 0.5f);
			}
			else if (rrDebug == 7 || rrDebug == 8)
			{
				float distance = rrDebug == 7 ? rrDepth[guidePixel] : rrSpecularDistance[guidePixel];
				color = (distance / (1 + distance)).xxx;
			}
			outputImage[pixel] = float4(color, 1);
		}
		return;
	}
	if (restirPass < 2)
	{
		prepareRestir(pixel, state);
		return;
	}
	bool giCandidatePrepass = restirPass == 4;
	if (float(pixel.x) < viewportLeft)
	{
		if (!giCandidatePrepass)
		{
			outputImage[pixel] = float4(backgroundColor, 1.0f);
		}
		if (giEnabled != 0 && (giUseSpatial == 0 || giCandidatePrepass))
		{
			giCandidates[pixel.x + pixel.y * DispatchRaysDimensions().x] = (GiCandidate)0;
		}
		return;
	}
	float3 frameRadiance = 0.0f;
	float3 historyColor = 0;
	float3 spatialColor = 0;
	uint pathSeed = 0;
	uint recordedEndState = 0;
	uint recordedHash = 0;
	float3 recordedRadiance = 0;
	bool replayMismatch = false;
	uint longestPath = 0;
	uint passesPerSample = pathReplay != 0 ? 2u : 1u;
	uint pathCount = giCandidatePrepass ? 1u : samplesPerFrame * passesPerSample;
	for (uint pathIndex = 0; pathIndex < pathCount; ++pathIndex)
	{
		uint sampleIndex = pathIndex / passesPerSample;
		bool replaying = pathReplay != 0 && (pathIndex & 1u) != 0;
		if (replaying)
		{
			state = pathSeed;
		}
		else
		{
			pathSeed = state;
		}
		uint pathHash = 2166136261u;
		float2 dimensions = float2(DispatchRaysDimensions().xy);
		float2 jitter = fixedGiGuide || giView == 8 || giView == 10 ? 0.5f : (rrEnabled != 0 ? 0.5f + rrJitter : float2(random(state), random(state)));
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
		float previousBsdfPdf = 0.0f;
		bool previousDelta = true;
		bool previousRestir = false;
		float3 previousPosition = cameraPosition;
		GiCandidate candidate = (GiCandidate)0;
		float3 suffixThroughput = 1;
		float3 suffixRadiance = 0;
		float3 referenceDiffuse = 0;
		float3 diffuseRatio = 0;
		float3 connectionPosition = 0;
		bool hybridPrefix = false;
		uint connectionDepth = giEnabled == 5 ? 2u : 1u;
		for (uint depth = 0; depth <= maxBounces; ++depth)
		{
			Payload payload;
			payload.hit = 0;
			TraceRay(scene, RAY_FLAG_NONE, 0xff, 0, 0, 0, ray, payload);
			if (pathReplay != 0)
			{
				longestPath = max(longestPath, depth + 1u);
				recordPathVector(pathHash, ray.Direction);
			}
			float rectangleDistance;
			uint rectangleIndex = nearestRectangle(ray, rectangleDistance);
			if (!giCandidatePrepass && rrEnabled != 0 && sampleIndex == 0 && depth == 0)
			{
				writeRRGuides(pixel, ray, payload, rectangleIndex, rectangleDistance);
				if (rrDebug >= 2)
				{
					return;
				}
			}
			if (rectangleIndex < rectangleCount
				&& (payload.hit == 0 || rectangleDistance < dot(payload.position - ray.Origin, ray.Direction)))
			{
				RectangleLight rectangle = rectangles[rectangleIndex];
				if (pathReplay != 0)
				{
					recordPathValue(pathHash, 1u);
					recordPathValue(pathHash, rectangleIndex);
					recordPathValue(pathHash, asuint(rectangleDistance));
				}
				float weight = 1.0f;
				if (depth > 0 && !previousDelta && rectangle.mis != 0)
				{
					if (previousRestir && rectangle.selectionPdf > 0)
					{
						weight = 0;
					}
					else
					{
						float pdf = rectangle.selectionPdf * rectanglePdf(rectangle, previousPosition, ray.Origin + ray.Direction * rectangleDistance);
						if (pathReplay != 0)
						{
							recordPathValue(pathHash, asuint(pdf));
						}
						weight = powerHeuristic(previousBsdfPdf, pdf);
					}
				}
				addGiPathRadiance(rectangleEmission(rectangle, ray.Direction) * weight, throughput,
					candidate.valid != 0 && depth > connectionDepth, suffixThroughput, diffuseRatio, radiance, suffixRadiance, referenceDiffuse);
				break;
			}
			if (payload.hit == 0)
			{
				if (pathReplay != 0)
				{
					recordPathValue(pathHash, 0u);
				}
				addGiPathRadiance(payload.color * missWeight, throughput,
					candidate.valid != 0 && depth > connectionDepth, suffixThroughput, diffuseRatio, radiance, suffixRadiance, referenceDiffuse);
				break;
			}
			Material material = materials[payload.materialIndex];
			if (pathReplay != 0)
			{
				recordPathValue(pathHash, 2u);
				recordPathValue(pathHash, payload.triangleIndex);
				recordPathVector(pathHash, payload.position);
			}
			Surface surface = pathSurface(payload, material);
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
			if (giEnabled >= 2 && depth == 0 && maxBounces > 0)
			{
				candidate.sourcePosition = surface.position;
				candidate.sourceTriangle = payload.triangleIndex;
				candidate.sourceNormal = surface.normal;
				candidate.sourceGeometricNormal = surface.geometricNormal;
				candidate.sourceMaterial = payload.materialIndex;
				candidate.sourceBaseColor = surface.baseColor;
				candidate.sourceView = view;
				bool diffuseSource = surface.metallic < 1
					&& maxComponent(surface.baseColor * (1 - fresnelSchlick(materialF0(surface), dot(surface.normal, view)))) > 0;
				bool supportedSource = giEnabled == 5 ? surface.roughness < 0.2f && maxBounces >= 2
					: surface.roughness >= 0.2f && (giEnabled == 4 || diffuseSource);
				candidate.sourceValid = supportedSource && surface.occlusion > 0 ? 1u : 0u;
			}
			if (giEnabled != 0 && depth == connectionDepth && candidate.solidAnglePdf > 0)
			{
				candidate.position = payload.position;
				candidate.normal = payload.geometricNormal;
				candidate.areaPdf = giAreaPdf(candidate.solidAnglePdf, connectionPosition, candidate.position, candidate.normal);
				candidate.valid = candidate.areaPdf > 0 && isfinite(candidate.areaPdf) ? 1u : 0u;
				// Exact direction-independent radiance only: flat-normal Lambertian secondary surfaces.
				candidate.reusable = material.metallic == 0 && material.specular == 0 && payload.flatNormal != 0 ? 1u : 0u;
			}
			if (giCandidatePrepass && (candidate.sourceValid == 0 || (depth == 1 && (candidate.valid == 0 || candidate.reusable == 0))))
			{
				// Keep the primary proposal even if its raw draw cannot be reused.
				break;
			}
			float emissionWeight = 1.0f;
			if (depth > 0 && !previousDelta && emissiveMis != 0 && meshLightCount > 0)
			{
				if (previousRestir && meshLights[payload.triangleIndex].areaDensity > 0)
				{
					emissionWeight = 0;
				}
				else
				{
					float pdf = meshLightPdf(meshLights[payload.triangleIndex], previousPosition, payload.position);
					if (pathReplay != 0)
					{
						recordPathValue(pathHash, asuint(pdf));
					}
					emissionWeight = powerHeuristic(previousBsdfPdf, pdf);
				}
			}
			// Emission at the secondary surface is direct lighting of the primary:
			// keep it (and its existing MIS weight) outside the diffuse GI candidate.
			addGiPathRadiance(material.emissiveColor * material.emissiveStrength * emissionWeight, throughput,
				candidate.valid != 0 && depth > connectionDepth, suffixThroughput, diffuseRatio, radiance, suffixRadiance, referenceDiffuse);
			addGiPathRadiance(directLighting(surface, view), throughput, candidate.valid != 0,
				suffixThroughput, diffuseRatio, radiance, suffixRadiance, referenceDiffuse);
			if (depth == maxBounces)
			{
				break;
			}
			bool useRestir = false;
			if (depth == 0 && restirMode > 0 && meshletColors < 0.5f)
			{
				PrimarySurface primary = primarySurfaces[pixel.x + pixel.y * DispatchRaysDimensions().x];
				useRestir = primary.valid != 0 && primary.triangleIndex == payload.triangleIndex
					&& length(primary.position - surface.position) < max(0.01f, primary.depth * 0.02f);
			}
			if (useRestir)
			{
				addGiPathRadiance(restirLighting(surface, view, pixel), throughput, candidate.valid != 0,
					suffixThroughput, diffuseRatio, radiance, suffixRadiance, referenceDiffuse);
			}
			if (!useRestir && environmentMis != 0 && environmentIntensity > 0.0f)
			{
				float2 lightU = float2(random(state), random(state));
				float proposalPdf;
				float3 light = environmentLighting(surface, view, lightU, proposalPdf);
				if (pathReplay != 0)
				{
					recordPathValue(pathHash, asuint(proposalPdf));
				}
				addGiPathRadiance(light, throughput, candidate.valid != 0,
					suffixThroughput, diffuseRatio, radiance, suffixRadiance, referenceDiffuse);
			}
			if (!useRestir && sampledRectangleCount > 0)
			{
				uint lightIndex = sampleRectangleIndex(random(state));
				float2 lightU = float2(random(state), random(state));
				if (lightIndex < rectangleCount)
				{
					float proposalPdf;
					float3 light = rectangleLighting(surface, view, rectangles[lightIndex], lightU, proposalPdf);
					if (pathReplay != 0)
					{
						recordPathValue(pathHash, asuint(proposalPdf));
					}
					addGiPathRadiance(light, throughput, candidate.valid != 0,
						suffixThroughput, diffuseRatio, radiance, suffixRadiance, referenceDiffuse);
				}
			}
			if (!useRestir && emissiveMis != 0 && meshLightCount > 0)
			{
				float3 lightU = float3(random(state), random(state), random(state));
				float proposalPdf;
				float3 light = emissiveLighting(surface, view, lightU, payload.triangleIndex, proposalPdf);
				if (pathReplay != 0)
				{
					recordPathValue(pathHash, asuint(proposalPdf));
				}
				addGiPathRadiance(light, throughput, candidate.valid != 0,
					suffixThroughput, diffuseRatio, radiance, suffixRadiance, referenceDiffuse);
			}
			float3 nextDirection, weight;
			float3 u = float3(random(state), random(state), random(state));
			if (giEnabled == 5 && depth == 0 && candidate.sourceValid != 0)
			{
				candidate.prefixSample = u;
				hybridPrefix = u.z < specularProbability(surface, view);
			}
			if (!sampleBsdf(surface, view, u, nextDirection, weight))
			{
				break;
			}
			throughput *= weight;
			if (candidate.valid != 0)
			{
				suffixThroughput *= weight;
			}
			float survival = pathSurvivalProbability(throughput, depth + 1u, russianRoulette != 0);
			if (pathReplay != 0)
			{
				recordPathValue(pathHash, asuint(survival));
				float proposalPdf;
				float lobeProbability = specularProbability(surface, view);
				bool deltaSample = surface.roughness < 0.001f && u.z < lobeProbability;
				evaluateBsdf(surface, view, nextDirection, proposalPdf);
				recordPathValue(pathHash, deltaSample ? 1u : 0u);
				recordPathValue(pathHash, asuint(deltaSample ? lobeProbability : proposalPdf));
			}
			if (survival < 1.0f)
			{
				if (!surviveRoulette(throughput, survival, random(state)))
				{
					break;
				}
				if (candidate.valid != 0)
				{
					suffixThroughput /= survival;
				}
			}
			missWeight = 1.0f;
			bool delta = surface.roughness < 0.001f && u.z < specularProbability(surface, view);
			if (giEnabled != 0 && giEnabled != 5 && depth == 0 && !delta)
			{
				float pdf;
				evaluateBsdf(surface, view, nextDirection, pdf);
				float3 factor = giDiffuseBrdfCosine(surface, view, nextDirection);
				if (giEnabled == 4)
				{
					// The full-lobe mode reconnects finite primary BSDFs, including rough metals.
					factor = candidate.sourceValid != 0 ? weight * pdf : 0;
				}
				if (pdf > 1e-10f && (maxComponent(factor) > 0 || (giEnabled >= 2 && candidate.sourceValid != 0)))
				{
					candidate.sourcePosition = surface.position;
					candidate.sourceTriangle = payload.triangleIndex;
					candidate.solidAnglePdf = pdf;
					candidate.inversePdf = rcp(pdf);
					candidate.diffuseBrdfCosine = factor;
					connectionPosition = surface.position;
					diffuseRatio = giEnabled == 4 ? 1 : select(weight > 0, factor / pdf / max(weight, 1e-30f), 0);
				}
			}
			if (giEnabled == 5 && depth == 1 && hybridPrefix && surface.roughness >= 0.2f)
			{
				float pdf;
				evaluateBsdf(surface, view, nextDirection, pdf);
				candidate.prefixTriangle = payload.triangleIndex;
				candidate.solidAnglePdf = pdf;
				candidate.inversePdf = rcp(pdf);
				candidate.diffuseBrdfCosine = throughput * pdf;
				connectionPosition = surface.position;
				diffuseRatio = 1;
			}
			if (environmentMis != 0 && environmentIntensity > 0.0f && !delta)
			{
				float bsdfPdf;
				evaluateBsdf(surface, view, nextDirection, bsdfPdf);
				uint width, height;
				environmentMap.GetDimensions(width, height);
				float lightPdf = environmentPdf(environmentDistribution, uint2(width, height), nextDirection, environmentRotation);
				if (pathReplay != 0)
				{
					recordPathValue(pathHash, asuint(lightPdf));
				}
				if (useRestir)
				{
					lightPdf *= restirEnvironmentSampleRatio();
				}
				// ReSTIR estimates the light-side MIS term; keep its BSDF complement.
				missWeight = environmentReservoirHitWeight(lightPdf, bsdfPdf, delta);
			}
			previousPosition = surface.position;
			previousDelta = delta;
			previousRestir = useRestir;
			previousBsdfPdf = 0.0f;
			if (!delta)
			{
				evaluateBsdf(surface, view, nextDirection, previousBsdfPdf);
			}
			ray.Direction = nextDirection;
			ray.Origin = offsetOrigin(payload.position, payload.geometricNormal);
			ray.TMin = 0.0f;
		}
		if (pathReplay != 0)
		{
			if (replaying)
			{
				replayMismatch = replayMismatch || pathHash != recordedHash || state != recordedEndState
					|| any(asuint(radiance) != asuint(recordedRadiance)) || !all(isfinite(radiance));
				state = recordedEndState;
				continue;
			}
			recordedHash = pathHash;
			recordedEndState = state;
			recordedRadiance = radiance;
		}
		candidate.radiance = suffixRadiance;
		candidate.targetDensity = dot(max(candidate.diffuseBrdfCosine * suffixRadiance, 0), float3(0.2126f, 0.7152f, 0.0722f));
		if (giCandidatePrepass)
		{
			giCandidates[pixel.x + pixel.y * DispatchRaysDimensions().x] = candidate;
			return;
		}
		float3 diffuseGi = shadeGiCandidate(candidate);
		if (giEnabled >= 2 && candidate.reusable == 0)
		{
			// The non-Lambertian part remains in ordinary PT, including fresh misses.
			diffuseGi = 0;
			referenceDiffuse = 0;
		}
		float3 remainder = radiance - referenceDiffuse;
		float3 freshGi = diffuseGi;
		float3 sampleHistoryColor = 0;
		float3 sampleSpatialColor = 0;
		if (giUseSpatial != 0)
		{
			diffuseGi = spatialGi(candidate, pixel, sampleIndex, sampleHistoryColor, sampleSpatialColor);
		}
		else if (giEnabled >= 2)
		{
			diffuseGi = temporalGi(candidate, pixel, sampleIndex, sampleHistoryColor);
		}
		// Replace the sampled diffuse indirect contribution; never add it twice.
		if (giEnabled != 0)
		{
			radiance = remainder + diffuseGi;
			if (sampleIndex == 0)
			{
				if (giUseSpatial == 0)
				{
					giCandidates[pixel.x + pixel.y * DispatchRaysDimensions().x] = candidate;
				}
				historyColor = sampleHistoryColor;
				spatialColor = sampleSpatialColor;
			}
		}
		if (giView == 1 || giView == 9)
		{
			radiance = diffuseGi;
		}
		else if (giView == 2)
		{
			radiance = max(remainder, 0);
		}
		else if (giView == 3)
		{
			radiance = candidate.valid != 0 ? suffixRadiance : 0;
		}
		else if (giView == 7)
		{
			radiance = abs(freshGi - referenceDiffuse) * 1000;
		}
		frameRadiance += radiance;
	}
	frameRadiance /= float(samplesPerFrame);
	float3 average = frameRadiance;
	if (frameIndex > 0 && rrEnabled == 0)
	{
		average = lerp(accumulation[pixel].rgb, frameRadiance, 1.0f / (float(frameIndex) + 1.0f));
	}
	accumulation[pixel] = float4(average, 1.0f);
	if (rrEnabled != 0)
	{
		rrColor[uint2(pixel.x - uint(viewportLeft), pixel.y)] = float4(frameRadiance, 1);
	}
	outputImage[pixel] = float4(displayColor(average), 1.0f);
	if (pathReplay == 2 || replayMismatch)
	{
		outputImage[pixel] = replayMismatch ? float4(1, 0, 1, 1) : float4(0, 1, longestPath > 1 ? 1 : 0, 1);
	}
	if (giView == 8)
	{
		outputImage[pixel] = float4(historyColor, 1);
	}
	else if (giView == 10)
	{
		outputImage[pixel] = float4(spatialColor, 1);
	}
	else if (giView == 9)
	{
		outputImage[pixel] = float4(displayColor(frameRadiance), 1);
	}
	if (fixedGiGuide)
	{
		GiCandidate guide = giCandidates[pixel.x + pixel.y * DispatchRaysDimensions().x];
		float3 color = 0;
		if (guide.valid != 0)
		{
			if (giView == 4)
			{
				color = guide.normal * 0.5f + 0.5f;
			}
			else if (giView == 5)
			{
				color = float3(guide.solidAnglePdf / (1 + guide.solidAnglePdf), guide.areaPdf / (1 + guide.areaPdf), 0);
			}
			else
			{
				color = float3(0, 1, 0);
			}
		}
		outputImage[pixel] = float4(color, 1);
	}
	if (restirDebug > 0)
	{
		uint address = pixel.x + pixel.y * DispatchRaysDimensions().x;
		PrimarySurface primary = primarySurfaces[address];
		float3 debugColor = 0;
		if (primary.valid != 0)
		{
			if (restirDebug == 1)
			{
				debugColor = primary.normal * 0.5f + 0.5f;
			}
			else if (restirDebug == 2)
			{
				debugColor = (primary.depth / (1.0f + primary.depth)).xxx;
			}
			else if (restirDebug == 3)
			{
				debugColor = float3(saturate(0.5f + primary.motion * 0.05f), 0.5f);
			}
			else
			{
				debugColor = float3(currentReservoirs[address].count / 200.0f, historyValid != 0 && restirMode >= 2 ? 1.0f : 0.0f, 0);
			}
		}
		outputImage[pixel] = float4(debugColor, 1);
	}
}
[shader("miss")]
void RMS_Main(inout Payload payload)
{
	payload.hit = 0;
	payload.color = sampleEnvironment(WorldRayDirection());
}
[shader("closesthit")]
void CHS_Main(inout Payload payload, in BuiltInTriangleIntersectionAttributes attributes)
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
	uint flatNormal = dot(normal - geometricNormal, normal - geometricNormal) < 1e-8f ? 1u : 0u;
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
	payload.triangleIndex = meshlets[meshletIndex].y / 3 + PrimitiveIndex();
	payload.flatNormal = flatNormal;
}
