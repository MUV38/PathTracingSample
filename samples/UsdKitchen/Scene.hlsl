RaytracingAccelerationStructure scene : register(t0);
struct Vertex
{
	float3 position;
	float3 normal;
	float3 color;
};
StructuredBuffer<Vertex> vertices : register(t1);
StructuredBuffer<uint> meshletVertices : register(t2);
StructuredBuffer<uint4> meshlets : register(t3);
ByteAddressBuffer meshletTriangles : register(t4);
RWTexture2D<float4> outputImage : register(u0);
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
};
struct Payload
{
	float3 color;
};
uint localIndex(uint offset)
{
	return (meshletTriangles.Load(offset & ~3u) >> ((offset & 3u) * 8u)) & 255u;
}
float3 displayColor(float3 radiance)
{
	float3 mapped = max(radiance * exposure, 0.0f);
	mapped = mapped / (1.0f + mapped);
	return select(mapped <= 0.0031308f, mapped * 12.92f, 1.055f * pow(mapped, 1.0f / 2.4f) - 0.055f);
}

float visibility(float3 position, float3 geometricNormal)
{
	if (shadows < 0.5f)
	{
		return 1.0f;
	}
	// Geometric-normal offset avoids sending rays beneath smoothed surfaces.
	// A relative term also keeps the offset representable far from the origin.
	float bias = max(shadowBias, 0.000001f * max(max(abs(position.x), abs(position.y)), abs(position.z)));
	RayDesc ray;
	ray.Origin = position + geometricNormal * bias;
	ray.TMin = 0.0f;
	ray.Direction = lightDirection;
	ray.TMax = 100000.0f;
	if (pointLight > 0.5f)
	{
		float3 delta = lightPosition - ray.Origin;
		float distance = length(delta);
		if (distance <= bias)
		{
			return 1.0f;
		}
		ray.Direction = delta / distance;
		ray.TMax = distance * 0.999999f;
	}
	RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
	query.TraceRayInline(scene, RAY_FLAG_NONE, 0xff, ray);
	// All geometry is opaque triangles, so no candidate requires shader handling.
	query.Proceed();
	return query.CommittedStatus() == COMMITTED_NOTHING ? 1.0f : 0.0f;
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
	float2 dimensions = float2(DispatchRaysDimensions().xy);
	float2 uv = float2((float(pixel.x) - viewportLeft + 0.5f) / max(dimensions.x - viewportLeft, 1.0f),
		(float(pixel.y) + 0.5f) / dimensions.y);
	float2 screen = uv * 2.0f - 1.0f;
	RayDesc ray;
	ray.Origin = cameraPosition;
	ray.Direction = normalize(cameraForward + screen.x * aspectRatio * tanHalfFov * cameraRight - screen.y * tanHalfFov * cameraUp);
	ray.TMin = 0.0001f;
	ray.TMax = 100000.0f;
	Payload payload;
	payload.color = 0.0f;
	TraceRay(scene, RAY_FLAG_NONE, 0xff, 0, 0, 0, ray, payload);
	outputImage[pixel] = float4(payload.color, 1.0f);
}
[shader("miss")]
void RMSMain(inout Payload payload)
{
	payload.color = backgroundColor * lerp(0.65f, 1.65f, saturate(WorldRayDirection().y * 0.5f + 0.5f));
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
	float3 color = a.color * weights.x + b.color * weights.y + c.color * weights.z;
	if (meshletColors > 0.5f)
	{
		uint hash = (meshletIndex + 1) * 747796405u + 2891336453u;
		hash = ((hash >> ((hash >> 28u) + 4u)) ^ hash) * 277803737u;
		hash = (hash >> 22u) ^ hash;
		color = 0.2f + 0.8f * float3(hash & 255u, (hash >> 8u) & 255u, (hash >> 16u) & 255u) / 255.0f;
	}
	float3 position = a.position + attributes.barycentrics.x * (b.position - a.position)
		+ attributes.barycentrics.y * (c.position - a.position);
	float3 toLight = lightDirection;
	float attenuation = 1.0f;
	if (pointLight > 0.5f)
	{
		float3 delta = lightPosition - position;
		float distanceSquared = max(dot(delta, delta), 0.0001f);
		toLight = delta * rsqrt(distanceSquared);
		attenuation = rcp(distanceSquared);
	}
	float cosine = saturate(dot(normal, toLight));
	float visible = 0.0f;
	if (cosine > 0.0f && dot(geometricNormal, toLight) > 0.0f && lightStrength > 0.0f)
	{
		visible = visibility(position, geometricNormal);
	}
	// Lambert BRDF = albedo / pi. The adjustable ambient term is only a preview fill.
	float3 irradiance = ambient + lightColor * lightStrength * attenuation * cosine * visible;
	payload.color = displayColor(color * (1.0f / 3.14159265f) * irradiance);
}
