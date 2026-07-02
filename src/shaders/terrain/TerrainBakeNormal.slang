struct TerrainBakeResolvePushConstants {
    int resolution;
    float texelWorldSize;
};
[[vk::push_constant]] ConstantBuffer<TerrainBakeResolvePushConstants> pc;

[[vk::binding(0, 0)]] RWTexture2D<float> paddedHeight;
[[vk::binding(1, 0)]] RWTexture2D<float> outHeight;
[[vk::binding(2, 0)]] RWTexture2D<float4> outNormal;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= uint(pc.resolution) || id.y >= uint(pc.resolution))
        return;

    int2 p = int2(id.xy) + int2(1, 1);

    float center = paddedHeight[p];
    float hL = paddedHeight[p + int2(-1, 0)];
    float hR = paddedHeight[p + int2(1, 0)];
    float hD = paddedHeight[p + int2(0, -1)];
    float hU = paddedHeight[p + int2(0, 1)];

    float3 normal = normalize(float3((hL - hR) / (2.0 * pc.texelWorldSize), (hD - hU) / (2.0 * pc.texelWorldSize), 1.0));

    outHeight[id.xy] = center;
    outNormal[id.xy] = float4(normal * 0.5 + 0.5, 1.0);
}
