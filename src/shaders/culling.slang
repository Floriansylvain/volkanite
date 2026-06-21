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

[[vk::binding(3, 2)]]
Sampler2D hiZPyramid;

struct CullCameraUniform {
    float4x4 view;
    float4x4 proj;
};
[[vk::binding(4, 2)]]
ConstantBuffer<CullCameraUniform> cullCamera;

[[vk::binding(6, 2)]]
RWStructuredBuffer<InstanceDataGPU> culledOutputInstances;

[[vk::binding(7, 2)]]
RWStructuredBuffer<DrawIndexedIndirectCommand> culledIndirectCommand;

[shader("compute")]
[numthreads(64, 1, 1)]
void csCullInstances(uint3 id: SV_DispatchThreadID) {
    if (id.x >= pc.instanceCount)
        return;

    float4 candidate = candidates[id.x];
    float3 boundingCenter = float3(pc.boundingCenterX, pc.boundingCenterY, pc.boundingCenterZ);
    float3 worldPos = candidate.xyz + boundingCenter;

    float radius = pc.boundingRadius;

    float4 viewPos4 = mul(cullCamera.view, float4(worldPos, 1.0));
    float3 viewPos = viewPos4.xyz;
    float dist = -viewPos.z;

    bool visible = true;

    if (dist > radius) {
        float nearestViewZ = viewPos.z + radius;

        float4 nearClip = mul(cullCamera.proj, float4(0.0, 0.0, nearestViewZ, 1.0));
        float nearestNdcDepth = nearClip.z / nearClip.w;

        float4 centerClip = mul(cullCamera.proj, viewPos4);
        float2 centerNdc = centerClip.xy / centerClip.w;

        float ndcRadius = (abs(cullCamera.proj[1][1]) * radius / dist);
        float2 uvCenter = centerNdc * float2(0.5, 0.5) + float2(0.5, 0.5);

        float footprintTexels = 2.0 * ndcRadius * float(pc.dstSize.y);
        float mipLevel = clamp(ceil(log2(max(footprintTexels, 1.0))) + 1.0, 0.0, float(pc.maxMip));

        float hiZDepth = hiZPyramid.SampleLevel(uvCenter, mipLevel).r;

        visible = (nearestNdcDepth <= hiZDepth);
    }

    if (visible) {
        uint slot;
        InterlockedAdd(indirectCommand[0].instanceCount, 1, slot);
        outputInstances[slot].position = candidate.xyz;
        outputInstances[slot].rotation = candidate.w;
    } else {
        uint culledSlot;
        InterlockedAdd(culledIndirectCommand[0].instanceCount, 1, culledSlot);
        culledOutputInstances[culledSlot].position = candidate.xyz;
        culledOutputInstances[culledSlot].rotation = candidate.w;
    }
}
