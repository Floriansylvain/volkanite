struct PyramidPushConstants {
    int2 srcSize;
    int2 dstSize;
    uint instanceCount;
    float time;
    float boundingRadius;
    uint maxMip;
    float boundingCenterX;
    float boundingCenterY;
    float boundingCenterZ;
    uint occlusionEnabled;
};
[[vk::push_constant]] ConstantBuffer<PyramidPushConstants> pc;

[[vk::binding(0, 1)]] RWTexture2D<float> srcMip;
[[vk::binding(1, 1)]] RWTexture2D<float> dstMip;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= uint(pc.dstSize.x) || id.y >= uint(pc.dstSize.y))
        return;

    const int2 srcMax = pc.srcSize - int2(1, 1);
    const int2 base = int2(id.xy) * 2;

    float d00 = srcMip[clamp(base + int2(0, 0), int2(0, 0), srcMax)];
    float d10 = srcMip[clamp(base + int2(1, 0), int2(0, 0), srcMax)];
    float d01 = srcMip[clamp(base + int2(0, 1), int2(0, 0), srcMax)];
    float d11 = srcMip[clamp(base + int2(1, 1), int2(0, 0), srcMax)];

    dstMip[id.xy] = max(max(d00, d10), max(d01, d11));
}
