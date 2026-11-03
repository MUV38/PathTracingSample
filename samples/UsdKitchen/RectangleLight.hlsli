// Analytic opaque rectangle. axisU / axisV are orthonormal; cross(U,V)
// points toward the emitting side. Shared by rendering and GPU tests.
struct RectangleLight
{
	float3 center;
	uint enabled;
	float3 axisU;
	float width;
	float3 axisV;
	float height;
	float3 radiance;
	uint mis;
	float selectionPdf;
	float cdf;
	float2 padding;
};
float3 rectangleNormal(RectangleLight light)
{
	return cross(light.axisU, light.axisV);
}
bool intersectRectangle(RectangleLight light, float3 origin, float3 direction, float tMin, float tMax, out float distance)
{
	distance = 0.0f;
	if (light.enabled == 0)
	{
		return false;
	}
	float3 normal = rectangleNormal(light);
	float denominator = dot(normal, direction);
	if (abs(denominator) < 0.0000001f)
	{
		return false;
	}
	distance = dot(light.center - origin, normal) / denominator;
	if (distance < tMin || distance > tMax)
	{
		return false;
	}
	float3 delta = origin + direction * distance - light.center;
	return abs(dot(delta, light.axisU)) <= light.width * 0.5f
		&& abs(dot(delta, light.axisV)) <= light.height * 0.5f;
}
float rectanglePdf(RectangleLight light, float3 origin, float3 lightPoint)
{
	float3 delta = lightPoint - origin;
	float distanceSquared = dot(delta, delta);
	if (light.enabled == 0 || distanceSquared <= 0.000000000001f)
	{
		return 0.0f;
	}
	float cosine = dot(rectangleNormal(light), -delta * rsqrt(distanceSquared));
	if (cosine <= 0.0f)
	{
		return 0.0f;
	}
	return distanceSquared / (light.width * light.height * cosine);
}
float3 sampleRectanglePoint(RectangleLight light, float2 u)
{
	return light.center + (u.x - 0.5f) * light.width * light.axisU + (u.y - 0.5f) * light.height * light.axisV;
}
float3 rectangleEmission(RectangleLight light, float3 direction)
{
	return dot(rectangleNormal(light), -direction) > 0.0f ? light.radiance : 0.0f;
}
uint sampleRectangleLight(RectangleLight lights[32], uint count, float u)
{
	for (uint i = 0; i < count; ++i)
	{
		if (u < lights[i].cdf)
		{
			return i;
		}
	}
	return count;
}
uint nearestRectangleLight(RectangleLight lights[32], uint count, float3 origin, float3 direction,
	float tMin, float tMax, out float distance)
{
	distance = tMax;
	uint nearest = count;
	for (uint i = 0; i < count; ++i)
	{
		float hitDistance;
		if (intersectRectangle(lights[i], origin, direction, tMin, distance, hitDistance))
		{
			distance = hitDistance;
			nearest = i;
		}
	}
	return nearest;
}
