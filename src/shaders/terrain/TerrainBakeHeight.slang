struct TerrainBakePushConstants {
    float2 chunkMin;
    float2 noiseOffset;
    float texelWorldSize;
    int paddedResolution;
    float noiseScale;
    float heightScale;
    float baseHeight;
    float persistence;
    float lacunarity;
    int octaves;
    float ridgeSharpness;
    float heightRedistribution;
    float regionScale;
    float regionThreshold;
    float regionBlendWidth;
    float flatScale;
    float flatThreshold;
    float flatBlendWidth;
    float minRelief;
    float unusedPadding;
};
[[vk::push_constant]] ConstantBuffer<TerrainBakePushConstants> pc;

[[vk::binding(0, 0)]] RWTexture2D<float> paddedHeight;

float2 terrainGradient(float2 p) {
    float2 hashed = float2(dot(p, float2(127.1, 311.7)), dot(p, float2(269.5, 183.3)));
    return -1.0 + 2.0 * frac(sin(hashed) * 43758.5453123);
}

float terrainQuinticFade(float t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

float terrainGradientNoise2D(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);

    float2 g00 = terrainGradient(i + float2(0.0, 0.0));
    float2 g10 = terrainGradient(i + float2(1.0, 0.0));
    float2 g01 = terrainGradient(i + float2(0.0, 1.0));
    float2 g11 = terrainGradient(i + float2(1.0, 1.0));

    float v00 = dot(g00, f - float2(0.0, 0.0));
    float v10 = dot(g10, f - float2(1.0, 0.0));
    float v01 = dot(g01, f - float2(0.0, 1.0));
    float v11 = dot(g11, f - float2(1.0, 1.0));

    float u = terrainQuinticFade(f.x);
    float v = terrainQuinticFade(f.y);

    float x1 = lerp(v00, v10, u);
    float x2 = lerp(v01, v11, u);
    return lerp(x1, x2, v);
}

float terrainRegionMask(float2 worldPos) {
    float2 base = worldPos / max(pc.regionScale, 1.0);
    float n = saturate(terrainGradientNoise2D(base) * 0.5 + 0.5);
    float halfWidth = max(pc.regionBlendWidth, 0.001);
    return smoothstep(pc.regionThreshold - halfWidth, pc.regionThreshold + halfWidth, n);
}

float terrainReliefMask(float2 worldPos) {
    float2 base = worldPos / max(pc.flatScale, 1.0) + float2(91.7, -57.3);
    float n = saturate(terrainGradientNoise2D(base) * 0.5 + 0.5);
    float halfWidth = max(pc.flatBlendWidth, 0.001);
    return smoothstep(pc.flatThreshold - halfWidth, pc.flatThreshold + halfWidth, n);
}

float terrainHeight(float2 worldPos) {
    float amplitude = 1.0;
    float frequency = 1.0;
    float hills = 0.0;
    float mountains = 0.0;
    float2 base = worldPos / pc.noiseScale + pc.noiseOffset;

    float sharpness = saturate(pc.ridgeSharpness);
    float ridgeExponent = lerp(1.0, 6.0, sharpness);

    for (int i = 0; i < pc.octaves; i++) {
        float n = terrainGradientNoise2D(base * frequency);
        float ridgeShape = pow(saturate(1.0 - abs(n)), ridgeExponent) * 2.0 - 1.0;

        hills += n * amplitude;
        mountains += lerp(n, ridgeShape, sharpness) * amplitude;

        amplitude *= pc.persistence;
        frequency *= pc.lacunarity;
    }

    float mountainAmount = terrainRegionMask(worldPos);
    float combined = lerp(hills, mountains, mountainAmount);

    float relief = lerp(saturate(pc.minRelief), 1.0, terrainReliefMask(worldPos));
    float shaped = combined * relief;

    float signH = shaped < 0.0 ? -1.0 : 1.0;
    float h = signH * pow(abs(shaped), max(pc.heightRedistribution, 0.0001));

    return h * pc.heightScale + pc.baseHeight;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= uint(pc.paddedResolution) || id.y >= uint(pc.paddedResolution))
        return;

    float2 worldPos = pc.chunkMin + float2(id.xy) * pc.texelWorldSize;
    paddedHeight[id.xy] = terrainHeight(worldPos);
}
