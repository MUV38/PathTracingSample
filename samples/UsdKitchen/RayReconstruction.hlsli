// Directional GGX reflectance approximation from the NVIDIA DLSS-RR guide
// (EnvBRDFApprox2 / Ray Tracing Gems, chapter 32), plus this renderer's
// integrated multiple-scattering compensation.
float3 rrSpecularReflectance(Surface surface, float noV)
{
	float3 f0 = materialF0(surface);
	if (surface.roughness < 0.001f)
	{
		return fresnelSchlick(f0, noV);
	}
	float alpha = surface.roughness * surface.roughness;
	float4 x = float4(1, noV, noV * noV, noV * noV * noV);
	float3 y = float3(1, alpha, alpha * alpha);
	float bias = dot(mul(float2x2(0.99044f,-1.28514f,1.29678f,-0.755907f), x.xy), y.xy)
		/ dot(mul(float3x3(1,2.92338f,59.4188f,20.3225f,-27.0302f,222.592f,121.563f,626.13f,316.627f), x.xyw), y);
	float scale = dot(mul(float2x2(0.0365463f,3.32707f,9.0632f,-9.04756f), x.xy), y.xy)
		/ dot(mul(float3x3(1,3.59685f,-1.36772f,9.04401f,-16.3174f,9.22949f,5.56589f,19.7886f,-20.2123f), x.xzw), y);
	float3 result = f0 * max(0, scale) + max(0, bias) * saturate(f0.g * 50);
	if (!surface.singleScatterOnly)
	{
		float2 loss = ggxLoss(max(surface.roughness, 0.02f), noV);
		result += ggxMultipleFresnel(f0, loss.y) * loss.x;
	}
	return saturate(result);
}
float rrReflectionDistance(Surface surface, float3 view, uint2 pixel)
{
	if (maxComponent(materialF0(surface)) <= 0)
	{
		return 0;
	}
	uint guideFrame = rrDebug >= 2 ? 0u : rrFrame;
	uint state = pixel.x + pixel.y * DispatchRaysDimensions().x + (guideFrame + 1u) * 0x9e3779b9u;
	float3 direction, unusedWeight;
	// Select the finite GGX lobe explicitly; this guide uses its own RNG.
	if (!sampleBsdf(surface, view, float3(random(state), random(state), 0), direction, unusedWeight))
	{
		return 0;
	}
	RayDesc ray;
	ray.Origin = offsetOrigin(surface.position, surface.geometricNormal);
	ray.Direction = direction;
	ray.TMin = 0;
	ray.TMax = 100000;
	float distance;
	nearestRectangle(ray, distance);
	RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
	query.TraceRayInline(scene, RAY_FLAG_NONE, 0xff, ray);
	query.Proceed();
	if (query.CommittedStatus() != COMMITTED_NOTHING)
	{
		distance = min(distance, query.CommittedRayT());
	}
	// Encode a missing finite reflection hit as zero.
	return distance < ray.TMax ? distance : 0;
}
void writeRRGuides(uint2 pixel, RayDesc ray, Payload payload, uint rectangleIndex, float rectangleDistance)
{
	float3 position = ray.Origin + ray.Direction * ray.TMax;
	float3 normal = 0;
	float roughness = 1;
	float3 diffuseAlbedo = 0, specularAlbedo = 0;
	float specularDistance = 0;
	bool rectangleHit = rectangleIndex < rectangleCount
		&& (payload.hit == 0 || rectangleDistance < dot(payload.position - ray.Origin, ray.Direction));
	if (rectangleHit)
	{
		position = ray.Origin + ray.Direction * rectangleDistance;
		normal = rectangleNormal(rectangles[rectangleIndex]);
		if (dot(normal, ray.Direction) > 0)
		{
			normal = -normal;
		}
	}
	else if (payload.hit != 0)
	{
		Material material = materials[payload.materialIndex];
		Surface surface = (Surface)0;
		surface.position = payload.position;
		surface.normal = payload.normal;
		surface.geometricNormal = payload.geometricNormal;
		surface.baseColor = saturate(payload.color * material.baseColor);
		surface.metallic = material.metallic;
		surface.roughness = material.roughness;
		surface.specular = material.specular;
		surface.occlusion = material.occlusion;
		surface.singleScatterOnly = multipleScattering == 0;
		float noV = saturate(dot(surface.normal, -ray.Direction));
		float3 f0 = materialF0(surface);
		float f90 = saturate(50 * maxComponent(f0));
		float3 averageFresnel = f0 + (f90 - f0) / 21.0f;
		diffuseAlbedo = surface.baseColor * (1 - surface.metallic)
			* (1 - fresnelSchlick(f0, noV)) * (1 - averageFresnel);
		specularAlbedo = rrSpecularReflectance(surface, noV);
		specularDistance = rrReflectionDistance(surface, -ray.Direction, pixel);
		position = surface.position;
		normal = surface.normal;
		roughness = surface.roughness;
	}
	// Preserve vectors pointing outside the previous viewport at disocclusions.
	float4 previousClip = mul(float4(position, 1), previousViewProjection);
	float2 previousUV = previousClip.xy / max(previousClip.w, 0.000001f) * float2(0.5f, -0.5f) + 0.5f;
	float2 previous = previousUV * float2(DispatchRaysDimensions().x - previousViewportLeft, DispatchRaysDimensions().y);
	float2 current = float2(pixel.x - uint(viewportLeft), pixel.y) + 0.5f + rrJitter;
	float2 motion = rrReset == 0 && previousClip.w > 0 ? previous - current : 0;
	uint2 guidePixel = uint2(pixel.x - uint(viewportLeft), pixel.y);
	rrDepth[guidePixel] = rectangleHit || payload.hit != 0 ? max(0.001f, dot(position - cameraPosition, cameraForward)) : 100000;
	rrMotion[guidePixel] = motion;
	rrNormalRoughness[guidePixel] = float4(normal, roughness);
	rrDiffuseAlbedo[guidePixel] = float4(diffuseAlbedo, 1);
	rrSpecularAlbedo[guidePixel] = float4(specularAlbedo, 1);
	rrSpecularDistance[guidePixel] = specularDistance;
}
