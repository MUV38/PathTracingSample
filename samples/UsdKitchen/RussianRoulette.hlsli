// Apply after collecting this vertex's emission / direct light / NEE and
// updating throughput with the next BSDF sample. PDF values remain conditional
// BSDF PDFs; the independent roulette decision is compensated in throughput.
float pathSurvivalProbability(float3 throughput, uint completedBounces, bool enabled)
{
	float maximum = max(throughput.x, max(throughput.y, throughput.z));
	if (maximum <= 0.0f)
	{
		return 0.0f;
	}
	if (!enabled || completedBounces < 3u)
	{
		return 1.0f;
	}
	return clamp(maximum, 0.05f, 1.0f);
}
bool surviveRoulette(inout float3 throughput, float probability, float u)
{
	if (u >= probability)
	{
		return false;
	}
	throughput /= probability;
	return true;
}
