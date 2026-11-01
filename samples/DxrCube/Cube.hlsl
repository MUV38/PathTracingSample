RaytracingAccelerationStructure scene : register(t0);
RWTexture2D<float4> outputImage : register(u0);

// Six float4 registers, matching SceneConstants in DxrCube.cpp.
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
	float3 albedo;
	float padding3;
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
	ray.Direction = normalize(cameraForward + screen.x * aspectRatio * tanHalfFov * cameraRight
		- screen.y * tanHalfFov * cameraUp);
	ray.TMin = 0.001f;
	ray.TMax = 10000.0f;
	Payload payload;
	payload.color = 0.0f;
	TraceRay(scene, RAY_FLAG_NONE, 0xff, 0, 1, 0, ray, payload);
	outputImage[pixel] = float4(payload.color, 1.0f);
}

[shader("miss")]
void RMSMain(inout Payload payload)
{
	float sky = saturate(WorldRayDirection().y * 0.5f + 0.5f);
	payload.color = backgroundColor * lerp(0.65f, 1.65f, sky);
}

[shader("closesthit")]
void CHSMain(inout Payload payload, in BuiltInTriangleIntersectionAttributes attributes)
{
	// Two triangles per face, in the same order as the index buffer.
	const float3 normals[6] = {
		float3(0, 0, -1), float3(0, 0, 1),
		float3(-1, 0, 0), float3(1, 0, 0),
		float3(0, -1, 0), float3(0, 1, 0)
	};
	float3 normal = normals[PrimitiveIndex() / 2];
	float diffuse = saturate(dot(normal, normalize(float3(-0.6f, 1.0f, 0.8f))));
	float3 linearColor = albedo * (0.18f + 0.82f * diffuse);
	// The swap chain is UNORM, so explicitly encode this simple lighting result for display.
	payload.color = pow(saturate(linearColor), 1.0f / 2.2f);
}
