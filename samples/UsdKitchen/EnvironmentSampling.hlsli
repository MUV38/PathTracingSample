// Shared by the path tracer and GPU white furnace tests.
static const float environmentPi = 3.14159265359f;
float powerHeuristic(float pdf, float otherPdf)
{
	float scale = max(pdf, otherPdf);
	if (scale <= 0.0f)
	{
		return 0.0f;
	}
	float a = pdf / scale, b = otherPdf / scale;
	return a * a / (a * a + b * b);
}
float2 environmentUv(float3 direction, float rotation)
{
	return float2(frac(atan2(direction.z, direction.x) / (2.0f * environmentPi) + 0.5f + rotation / (2.0f * environmentPi)),
		acos(clamp(direction.y, -1.0f, 1.0f)) / environmentPi);
}
uint findEnvironmentBin(StructuredBuffer<float2> distribution, uint offset, uint count, float u)
{
	uint low = 0, high = count - 1;
	while (low < high)
	{
		uint middle = (low + high) / 2;
		if (u < distribution[offset + middle].x)
		{
			high = middle;
		}
		else
		{
			low = middle + 1;
		}
	}
	return low;
}
float environmentPdf(StructuredBuffer<float2> distribution, uint2 dimensions, float3 direction, float rotation)
{
	uint2 pixel = min(uint2(environmentUv(direction, rotation) * dimensions), dimensions - 1);
	return distribution[dimensions.y + pixel.y * dimensions.x + pixel.x].y;
}
float3 sampleEnvironmentDirection(StructuredBuffer<float2> distribution, uint2 dimensions, float2 u, float rotation, out float pdf)
{
	uint y = findEnvironmentBin(distribution, 0, dimensions.y, u.y);
	uint offset = dimensions.y + y * dimensions.x;
	uint x = findEnvironmentBin(distribution, offset, dimensions.x, u.x);
	float rowStart = y == 0 ? 0.0f : distribution[y - 1].x;
	float columnStart = x == 0 ? 0.0f : distribution[offset + x - 1].x;
	float2 local = float2((u.x - columnStart) / (distribution[offset + x].x - columnStart),
		(u.y - rowStart) / (distribution[y].x - rowStart));
	float phi = ((float(x) + local.x) / float(dimensions.x) - 0.5f) * 2.0f * environmentPi - rotation;
	// Uniform solid angle inside the selected latitude/longitude cell.
	float cosine = lerp(cos(environmentPi * float(y) / float(dimensions.y)),
		cos(environmentPi * float(y + 1) / float(dimensions.y)), local.y);
	float sine = sqrt(max(0.0f, 1.0f - cosine * cosine));
	pdf = distribution[offset + x].y;
	return float3(sine * cos(phi), cosine, sine * sin(phi));
}
