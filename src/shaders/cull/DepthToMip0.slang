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

[[vk::binding(0, 0)]] Sampler2D srcDepth;
[[vk::binding(1, 0)]] RWTexture2D<float> dstMip0;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= uint(pc.dstSize.x) || id.y >= uint(pc.dstSize.y))
        return;

    float2 uv = (float2(id.xy) + 0.5) / float2(pc.dstSize);
    dstMip0[id.xy] = srcDepth.SampleLevel(uv, 0).r;
}
