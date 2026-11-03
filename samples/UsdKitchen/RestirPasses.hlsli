Surface primaryBsdf(PrimarySurface primary)
{
	Material material = materials[primary.materialIndex];
	Surface surface = (Surface)0;
	surface.position = primary.position;
	surface.normal = primary.normal;
	surface.geometricNormal = primary.geometricNormal;
	surface.baseColor = primary.baseColor;
	surface.metallic = material.metallic;
	surface.roughness = material.roughness;
	surface.specular = material.specular;
	surface.occlusion = material.occlusion;
	surface.singleScatterOnly = multipleScattering == 0;
	return surface;
}
float restirEnvironmentSampleRatio()
{
	// Eight initial candidates per frame; account for the light-type mixture
	// and the number of BSDF samples when defining the complementary MIS terms.
	float candidates = meshLightCount > 0 && emissiveMis != 0 ? 4.0f : 8.0f;
	return candidates / float(samplesPerFrame);
}
float3 reservoirIntegrand(Surface surface, float3 view, LightReservoir lightSample)
{
	if (lightSample.lightIndex == environmentLightIndex)
	{
		float3 direction = decodeEnvironmentSample(lightSample.uv);
		return evaluateEnvironmentReservoirSample(surface, view, direction, sampleEnvironment(direction), lightSample.proposalPdf * restirEnvironmentSampleRatio());
	}
	return evaluateReservoirSample(surface, view, meshLights[lightSample.lightIndex], lightSample.uv);
}
float reservoirProposalPdf(LightReservoir lightSample)
{
	bool meshEnabled = meshLightCount > 0 && emissiveMis != 0;
	bool environmentEnabled = environmentMis != 0 && environmentIntensity > 0;
	float typeProbability = meshEnabled && environmentEnabled ? 0.5f : 1.0f;
	if (lightSample.lightIndex == environmentLightIndex)
	{
		return typeProbability * lightSample.proposalPdf;
	}
	return typeProbability * meshLights[lightSample.lightIndex].areaDensity;
}
float reservoirTarget(Surface surface, float3 view, LightReservoir lightSample)
{
	float3 value = reservoirIntegrand(surface, view, lightSample);
	// A positive proposal-proportional floor gives every reservoir the same
	// full support, even across different BRDFs / horizons. Visibility is not
	// part of the selection target and is retraced for the final estimate.
	return lightSampleTarget(value, reservoirProposalPdf(lightSample));
}
void mergeReservoir(inout LightReservoir result, LightReservoir source, Surface surface, float3 view, float maximumCount, inout uint state)
{
	float count = min(source.count, maximumCount);
	if (count > 0 && source.weight > 0)
	{
		float target = reservoirTarget(surface, view, source);
		streamReservoir(result, source, target, target * source.weight * count, count, random(state));
	}
}
RayDesc primaryRay(uint2 pixel, float2 jitter)
{
	float2 dimensions = float2(DispatchRaysDimensions().xy);
	float2 uv = float2((float(pixel.x) - viewportLeft + jitter.x) / max(dimensions.x - viewportLeft, 1.0f),
		(float(pixel.y) + jitter.y) / dimensions.y);
	float2 screen = uv * 2.0f - 1.0f;
	RayDesc ray;
	ray.Origin = cameraPosition;
	ray.Direction = normalize(cameraForward + screen.x * aspectRatio * tanHalfFov * cameraRight - screen.y * tanHalfFov * cameraUp);
	ray.TMin = 0.0001f;
	ray.TMax = 100000.0f;
	return ray;
}
void prepareRestir(uint2 pixel, uint state)
{
	uint2 dimensions = DispatchRaysDimensions().xy;
	uint address = pixel.x + pixel.y * dimensions.x;
	if (restirPass == 1)
	{
		state ^= (restirFrame + 1u) * 0x85ebca6bu;
	}
	if (restirPass == 0)
	{
		PrimarySurface primary = (PrimarySurface)0;
		LightReservoir reservoir = (LightReservoir)0;
		if (float(pixel.x) >= viewportLeft)
		{
			float2 jitter = float2(random(state), random(state));
			RayDesc ray = primaryRay(pixel, jitter);
			Payload payload;
			payload.hit = 0;
			TraceRay(scene, RAY_FLAG_NONE, 0xff, 0, 0, 0, ray, payload);
			float rectangleDistance;
			bool rectangleHit = intersectRectangle(rectangle, ray.Origin, ray.Direction, ray.TMin, ray.TMax, rectangleDistance);
			if (payload.hit != 0 && (!rectangleHit || rectangleDistance > dot(payload.position - ray.Origin, ray.Direction)))
			{
				primary.valid = 1;
				primary.position = payload.position;
				primary.depth = dot(payload.position - cameraPosition, cameraForward);
				primary.normal = payload.normal;
				primary.geometricNormal = payload.geometricNormal;
				primary.materialIndex = payload.materialIndex;
				primary.triangleIndex = payload.triangleIndex;
				primary.baseColor = saturate(payload.color * materials[payload.materialIndex].baseColor);
				primary.roughness = materials[payload.materialIndex].roughness;
				float2 previousPixel = reprojectPrimary(primary.position, previousViewProjection, float2(dimensions), previousViewportLeft);
				primary.motion = previousPixel - (float2(pixel) + jitter);
				Surface surface = primaryBsdf(primary);
				float3 view = -ray.Direction;
				// Independent of path sampling and accumulation resets while moving.
				state = address ^ ((restirFrame + 1u) * 0xa511e9b3u);
				bool meshEnabled = meshLightCount > 0 && emissiveMis != 0;
				bool environmentEnabled = environmentMis != 0 && environmentIntensity > 0;
				if (restirMode > 0 && (meshEnabled || environmentEnabled) && maxBounces > 0 && meshletColors < 0.5f)
				{
					for (uint i = 0; i < 8; ++i)
					{
						LightReservoir candidate = (LightReservoir)0;
						bool selectEnvironment = environmentEnabled && (!meshEnabled || random(state) < 0.5f);
						if (selectEnvironment)
						{
							uint width, height;
							environmentMap.GetDimensions(width, height);
							float2 lightU = float2(random(state), random(state));
							float3 direction = sampleEnvironmentDirection(environmentDistribution, uint2(width,height), lightU, environmentRotation, candidate.proposalPdf);
							candidate.lightIndex = environmentLightIndex;
							candidate.uv = encodeEnvironmentSample(direction);
						}
						else
						{
							candidate.lightIndex = sampleMeshLightIndex(meshLights, meshLightCount, random(state));
							candidate.uv = float2(random(state), random(state));
						}
						float target = reservoirTarget(surface, view, candidate);
						float pdf = reservoirProposalPdf(candidate);
						streamReservoir(reservoir, candidate, target, pdf > 0 ? target / pdf : 0, 1, random(state));
					}
					finishReservoir(reservoir);
					if (restirMode >= 2 && historyValid != 0 && primary.roughness >= 0.1f
						&& all(previousPixel >= float2(previousViewportLeft, 0)) && all(previousPixel < float2(dimensions)))
					{
						uint2 previous = uint2(previousPixel);
						uint previousAddress = previous.x + previous.y * dimensions.x;
						if (compatiblePrimarySurfaces(primary, previousSurfaces[previousAddress]))
						{
							LightReservoir merged = (LightReservoir)0;
							mergeReservoir(merged, reservoir, surface, view, 8, state);
							mergeReservoir(merged, previousReservoirs[previousAddress], surface, view, 32, state);
							finishReservoir(merged);
							reservoir = merged;
						}
					}
				}
			}
		}
		primarySurfaces[address] = primary;
		initialReservoirs[address] = reservoir;
		return;
	}
	PrimarySurface primary = primarySurfaces[address];
	LightReservoir reservoir = initialReservoirs[address];
	if (restirMode >= 3 && primary.valid != 0 && primary.roughness >= 0.1f && reservoir.count > 0)
	{
		Surface surface = primaryBsdf(primary);
		float3 view = normalize(cameraPosition - primary.position);
		LightReservoir merged = (LightReservoir)0;
		mergeReservoir(merged, reservoir, surface, view, 40, state);
		for (uint i = 0; i < 4; ++i)
		{
			float radius = sqrt(random(state)) * 8;
			float phi = 2.0f * pi * random(state);
			int2 neighbor = int2(pixel) + int2(round(radius * float2(cos(phi), sin(phi))));
			if (all(neighbor >= int2(ceil(viewportLeft), 0)) && all(neighbor < int2(dimensions)) && any(neighbor != int2(pixel)))
			{
				uint neighborAddress = uint(neighbor.x) + uint(neighbor.y) * dimensions.x;
				if (compatiblePrimarySurfaces(primary, primarySurfaces[neighborAddress]))
				{
					mergeReservoir(merged, initialReservoirs[neighborAddress], surface, view, 40, state);
				}
			}
		}
		finishReservoir(merged);
		reservoir = merged;
	}
	currentReservoirs[address] = reservoir;
}
float3 restirLighting(Surface surface, float3 view, uint2 pixel)
{
	LightReservoir reservoir = currentReservoirs[pixel.x + pixel.y * DispatchRaysDimensions().x];
	if (reservoir.weight <= 0)
	{
		return 0;
	}
	float3 value = reservoirIntegrand(surface, view, reservoir);
	if (reservoir.lightIndex == environmentLightIndex)
	{
		return maxComponent(value) > 0 && visibleToEnvironment(surface, decodeEnvironmentSample(reservoir.uv)) ? value * reservoir.weight : 0;
	}
	float3 lightPoint = sampleMeshLightPoint(meshLights[reservoir.lightIndex], reservoir.uv);
	return maxComponent(value) > 0 && visibleToPoint(surface, lightPoint) ? value * reservoir.weight : 0;
}
