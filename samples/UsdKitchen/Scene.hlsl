RaytracingAccelerationStructure scene : register(t0);
struct Vertex
{
	float3 position;
	float3 normal;
	float3 color;
};
StructuredBuffer<Vertex> vertices : register(t1);
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
	float padding1;
	float3 backgroundColor;
	float padding2;
	float3 lightDirection;
	float exposure;
};
struct Payload
{
	float3 color;
};
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
	TraceRay(scene, RAY_FLAG_NONE, 0xff, 0, 1, 0, ray, payload);
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
	uint first = PrimitiveIndex() * 3;
	Vertex a = vertices[first];
	Vertex b = vertices[first + 1];
	Vertex c = vertices[first + 2];
	float3 weights = float3(1.0f - attributes.barycentrics.x - attributes.barycentrics.y, attributes.barycentrics);
	float3 normal = normalize(a.normal * weights.x + b.normal * weights.y + c.normal * weights.z);
	if (dot(normal, WorldRayDirection()) > 0.0f)
	{
		normal = -normal;
	}
	float3 color = a.color * weights.x + b.color * weights.y + c.color * weights.z;
	float diffuse = saturate(dot(normal, normalize(lightDirection)));
	payload.color = pow(saturate(color * (0.2f + 0.8f * diffuse) * exposure), 1.0f / 2.2f);
}
