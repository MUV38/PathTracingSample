// Kulla-Conty multiple-scattering approximation. LUT matches correlated Smith GGX.
StructuredBuffer<float> ggxEnergyLut : register(t9);
float2 ggxLoss(float roughness, float noV)
{
	float2 coordinate = float2(sqrt(saturate(noV)), saturate(roughness)) * 64.0f;
	uint2 cell = min(uint2(coordinate), 63u);
	float2 blend = coordinate - cell;
	uint offset = cell.y * 65u + cell.x;
	float loss = lerp(lerp(ggxEnergyLut[offset], ggxEnergyLut[offset + 1u], blend.x),
		lerp(ggxEnergyLut[offset + 65u], ggxEnergyLut[offset + 66u], blend.x), blend.y);
	float average = lerp(ggxEnergyLut[4225u + cell.y], ggxEnergyLut[4226u + cell.y], blend.y);
	return float2(loss, average);
}
float3 ggxMultipleFresnel(float3 f0, float averageLoss)
{
	// This renderer uses generalized Schlick: F90=0 when F0=0.
	float f90 = saturate(50.0f * max(f0.x, max(f0.y, f0.z)));
	float3 averageFresnel = f0 + (f90 - f0) / 21.0f;
	return averageFresnel * averageFresnel * (1.0f - averageLoss) / (1.0f - averageFresnel * averageLoss);
}
