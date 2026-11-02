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
struct Surface
{
	float3 position;
	float3 normal;
	float3 geometricNormal;
	float3 baseColor;
	float metallic;
	float roughness;
	float specular;
	float occlusion;
};
static const float pbrPi = 3.14159265359f;

float maxComponent(float3 value)
{
	return max(value.x, max(value.y, value.z));
}
float3 fresnelSchlick(float3 f0, float cosine)
{
	float f90 = saturate(50.0f * maxComponent(f0));
	float factor = pow(1.0f - saturate(cosine), 5.0f);
	return f0 + (f90 - f0) * factor;
}
float3 materialF0(Surface surface)
{
	return lerp((0.08f * surface.specular).xxx, surface.baseColor, surface.metallic);
}
float specularProbability(Surface surface, float3 view)
{
	float energy = maxComponent(fresnelSchlick(materialF0(surface), dot(surface.normal, view)));
	float diffuse = maxComponent(surface.baseColor) * (1.0f - surface.metallic) * (1.0f - energy);
	if (energy <= 0.0f)
	{
		return 0.0f;
	}
	if (diffuse <= 0.0f)
	{
		return 1.0f;
	}
	return clamp(energy / (energy + diffuse), 0.05f, 0.95f);
}
float ggxDistribution(float noH, float alpha)
{
	float a2 = alpha * alpha;
	float denominator = max(0.0f, 1.0f - noH * noH) + a2 * noH * noH;
	return a2 / (pbrPi * denominator * denominator);
}
float3 evaluateBsdf(Surface surface, float3 view, float3 direction, out float pdf)
{
	pdf = 0.0f;
	float noV = dot(surface.normal, view);
	float noL = dot(surface.normal, direction);
	float3 halfSum = view + direction;
	if (noV <= 0.0f || noL <= 0.0f || dot(halfSum, halfSum) < 0.00000001f)
	{
		return 0.0f;
	}
	float3 halfVector = normalize(halfSum);
	float noH = saturate(dot(surface.normal, halfVector));
	float voH = max(dot(view, halfVector), 0.000001f);
	float3 fresnel = fresnelSchlick(materialF0(surface), voH);
	float pSpecular = specularProbability(surface, view);
	float3 diffuse = (1.0f - fresnel) * (1.0f - surface.metallic) * surface.baseColor / pbrPi;
	pdf = (1.0f - pSpecular) * noL / pbrPi;
	// A smooth mirror is a delta distribution; it has no finite direct-light lobe.
	if (surface.roughness < 0.001f)
	{
		return diffuse;
	}
	float alpha = pow(max(surface.roughness, 0.02f), 2.0f);
	float a2 = alpha * alpha;
	float d = ggxDistribution(noH, alpha);
	float lambdaV = noL * sqrt(a2 + (1.0f - a2) * noV * noV);
	float lambdaL = noV * sqrt(a2 + (1.0f - a2) * noL * noL);
	float visibility = 0.5f / max(lambdaV + lambdaL, 0.00000001f);
	pdf += pSpecular * d * noH / (4.0f * voH);
	return diffuse + d * visibility * fresnel;
}
float3 tangentToWorld(float3 normal, float3 local)
{
	float3 tangent = normalize(cross(abs(normal.y) < 0.999f ? float3(0,1,0) : float3(1,0,0), normal));
	return normalize(tangent * local.x + cross(normal, tangent) * local.y + normal * local.z);
}
bool sampleBsdf(Surface surface, float3 view, float3 u, out float3 direction, out float3 weight)
{
	direction = 0.0f;
	weight = 0.0f;
	float pSpecular = specularProbability(surface, view);
	if (u.z < pSpecular)
	{
		if (surface.roughness < 0.001f)
		{
			direction = reflect(-view, surface.normal);
			weight = fresnelSchlick(materialF0(surface), dot(surface.normal, view)) * surface.occlusion / pSpecular;
			return dot(direction, surface.geometricNormal) > 0.0f;
		}
		float alpha = pow(max(surface.roughness, 0.02f), 2.0f);
		float cosine = sqrt((1.0f - u.x) / (1.0f - u.x + alpha * alpha * u.x));
		float sine = sqrt(max(0.0f, 1.0f - cosine * cosine));
		float phi = 2.0f * pbrPi * u.y;
		float3 halfVector = tangentToWorld(surface.normal, float3(sine * cos(phi), sine * sin(phi), cosine));
		if (dot(view, halfVector) <= 0.0f)
		{
			return false;
		}
		direction = reflect(-view, halfVector);
	}
	else
	{
		float radius = sqrt(u.x);
		float phi = 2.0f * pbrPi * u.y;
		direction = tangentToWorld(surface.normal, float3(radius * cos(phi), radius * sin(phi), sqrt(1.0f - u.x)));
	}
	if (dot(direction, surface.geometricNormal) <= 0.0f)
	{
		return false;
	}
	float pdf;
	float3 bsdf = evaluateBsdf(surface, view, direction, pdf);
	if (pdf <= 0.0000000001f)
	{
		return false;
	}
	weight = bsdf * saturate(dot(surface.normal, direction)) * surface.occlusion / pdf;
	return maxComponent(weight) > 0.0f;
}
