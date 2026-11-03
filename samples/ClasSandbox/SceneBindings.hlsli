// Six float4 registers, matching SceneConstants in ClasSandbox.cpp.
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
	uint meshletColors;
	float3 padding3;
};

struct Vertex
{
	float3 position;
	float3 normal;
	float3 color;
	uint materialIndex;
};

struct Material
{
	float3 baseColor;
	float metallic;
	float3 emissiveColor;
	float roughness;
	float specular;
	float occlusion;
	float emissiveStrength;
	float padding;
};

StructuredBuffer<Vertex> vertices : register(t1);
StructuredBuffer<uint4> meshlets : register(t2);
StructuredBuffer<uint> vertexReferences : register(t3);
ByteAddressBuffer triangleBytes : register(t4);
StructuredBuffer<Material> materials : register(t5);

uint triangleByte(uint offset)
{
	return (triangleBytes.Load(offset & ~3u) >> ((offset & 3u) * 8u)) & 255u;
}
