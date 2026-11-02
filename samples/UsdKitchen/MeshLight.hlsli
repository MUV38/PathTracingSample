struct MeshLight
{
	float3 position;
	float area;
	float3 edge1;
	float cdf;
	float3 edge2;
	float areaDensity;
	float3 radiance;
	uint materialIndex;
};
uint sampleMeshLightIndex(StructuredBuffer<MeshLight> lights, uint count, float u)
{
	uint low = 0, high = count - 1;
	while (low < high)
	{
		uint middle = (low + high) / 2;
		if (u < lights[middle].cdf)
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
float3 sampleMeshLightPoint(MeshLight light, float2 u)
{
	float root = sqrt(u.x);
	return light.position + root * (1.0f - u.y) * light.edge1 + root * u.y * light.edge2;
}
float meshLightPdf(MeshLight light, float3 origin, float3 lightPoint)
{
	float3 delta = lightPoint - origin;
	float distanceSquared = dot(delta, delta);
	if (light.area <= 0.0f || light.areaDensity <= 0.0f || distanceSquared <= 0.000000000001f)
	{
		return 0.0f;
	}
	float3 normal = cross(light.edge1, light.edge2) / (2.0f * light.area);
	// Existing emissive materials emit from both sides; NEE must match hits.
	float cosine = abs(dot(normal, delta * rsqrt(distanceSquared)));
	if (cosine <= 0.0f)
	{
		return 0.0f;
	}
	return light.areaDensity * distanceSquared / cosine;
}
