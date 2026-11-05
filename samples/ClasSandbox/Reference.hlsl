RaytracingAccelerationStructure scene : register(t0);
RWTexture2D<float4> outputImage : register(u0);

#include "SceneBindings.hlsli"

struct Payload
{
	float3 color;
};

[shader("raygeneration")]
void RGS_Main()
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
	ray.Direction = normalize(cameraForward + screen.x * aspectRatio * tanHalfFov * cameraRight
		- screen.y * tanHalfFov * cameraUp);
	ray.TMin = 0.001f;
	ray.TMax = 10000.0f;
	Payload payload;
	payload.color = 0.0f;
	// A single hit-group record is shared by all meshlet geometries.
	TraceRay(scene, RAY_FLAG_NONE, 0xff, 0, 0, 0, ray, payload);
	outputImage[pixel] = float4(payload.color, 1.0f);
}

[shader("miss")]
void RMS_Main(inout Payload payload)
{
	float sky = saturate(WorldRayDirection().y * 0.5f + 0.5f);
	payload.color = backgroundColor * lerp(0.65f, 1.65f, sky);
}

[shader("closesthit")]
void CHS_Main(inout Payload payload, in BuiltInTriangleIntersectionAttributes attributes)
{
	// Reference geometry order is exactly the SceneData meshlet order.
	uint meshletIndex = GeometryIndex();
	uint4 meshlet = meshlets[meshletIndex];
	uint triangleOffset = meshlet.y + PrimitiveIndex() * 3u;
	Vertex a = vertices[vertexReferences[meshlet.x + triangleByte(triangleOffset)]];
	Vertex b = vertices[vertexReferences[meshlet.x + triangleByte(triangleOffset + 1u)]];
	Vertex c = vertices[vertexReferences[meshlet.x + triangleByte(triangleOffset + 2u)]];
	float3 weights = float3(1.0f - attributes.barycentrics.x - attributes.barycentrics.y, attributes.barycentrics);
	float3 normal = a.normal * weights.x + b.normal * weights.y + c.normal * weights.z;
	if (dot(normal, normal) < 1e-12f)
	{
		normal = cross(b.position - a.position, c.position - a.position);
	}
	normal = normalize(normal);
	if (dot(normal, WorldRayDirection()) > 0.0f)
	{
		normal = -normal;
	}
	float3 albedo = a.color * weights.x + b.color * weights.y + c.color * weights.z;
	if (meshletColors != 0u)
	{
		uint hash = (meshletIndex + 1u) * 747796405u + 2891336453u;
		hash ^= InstanceID() * 0x9e3779b9u;
		hash = ((hash >> ((hash >> 28u) + 4u)) ^ hash) * 277803737u;
		hash = (hash >> 22u) ^ hash;
		albedo = 0.15f + 0.85f * float3(hash & 255u, (hash >> 8u) & 255u, (hash >> 16u) & 255u) / 255.0f;
	}
	float diffuse = saturate(dot(normal, normalize(float3(-0.6f, 1.0f, 0.8f))));
	float3 linearColor = albedo * (0.18f + 0.82f * diffuse);
	payload.color = pow(saturate(linearColor), 1.0f / 2.2f);
}
