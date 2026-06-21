struct PyramidPushConstants {
    int2 srcSize;
    int2 dstSize;
    uint instanceCount;
    float time;
};
[[vk::push_constant]] ConstantBuffer<PyramidPushConstants> pc;

[[vk::binding(0, 0)]]
Sampler2D srcDepth;

[[vk::binding(1, 0)]]
RWTexture2D<float> dstMip0;

[shader("compute")]
[numthreads(8, 8, 1)]
void csDepthToMip0(uint3 id : SV_DispatchThreadID) {
    if (id.x >= uint(pc.dstSize.x) || id.y >= uint(pc.dstSize.y))
        return;

    float2 uv = (float2(id.xy) + 0.5) / float2(pc.dstSize);
    dstMip0[id.xy] = srcDepth.SampleLevel(uv, 0).r;
}

[[vk::binding(0, 1)]]
RWTexture2D<float> srcMip;

[[vk::binding(1, 1)]]
RWTexture2D<float> dstMip;

[shader("compute")]
[numthreads(8, 8, 1)]
void csDownsample(uint3 id : SV_DispatchThreadID) {
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

struct InstanceDataGPU {
    float3 position;
    float rotation;
};

struct DrawIndexedIndirectCommand {
    uint32_t indexCount;
    uint32_t instanceCount;
    uint32_t firstIndex;
    int32_t vertexOffset;
    uint32_t firstInstance;
};

[[vk::binding(0, 2)]]
StructuredBuffer<float4> candidates;

[[vk::binding(1, 2)]]
RWStructuredBuffer<InstanceDataGPU> outputInstances;

[[vk::binding(2, 2)]]
RWStructuredBuffer<DrawIndexedIndirectCommand> indirectCommand;

[shader("compute")]
[numthreads(64, 1, 1)]
void csCullInstances(uint3 id: SV_DispatchThreadID) {
    if (id.x >= pc.instanceCount)
        return;

    float4 candidate = candidates[id.x];

    uint slot;
    InterlockedAdd(indirectCommand[0].instanceCount, 1, slot);
    outputInstances[slot].position = candidate.xyz;
    outputInstances[slot].rotation = pc.time * candidate.w;
}