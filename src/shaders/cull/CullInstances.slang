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

struct InstanceDataGPU {
    float3 position;
    float pad0;
    float3 rotation;
    float pad1;
    float2 uvScale;
    float pad2;
    float pad3;
};

struct DrawIndexedIndirectCommand {
    uint32_t indexCount;
    uint32_t instanceCount;
    uint32_t firstIndex;
    int32_t vertexOffset;
    uint32_t firstInstance;
};

[[vk::binding(0, 2)]] StructuredBuffer<InstanceDataGPU> candidates;
[[vk::binding(1, 2)]] RWStructuredBuffer<InstanceDataGPU> outputInstances;
[[vk::binding(2, 2)]] RWStructuredBuffer<DrawIndexedIndirectCommand> indirectCommand;
[[vk::binding(3, 2)]] Sampler2D hiZPyramid;

struct CullCameraUniform {
    float4x4 view;
    float4x4 proj;
};
[[vk::binding(4, 2)]] ConstantBuffer<CullCameraUniform> cullCamera;

[[vk::binding(6, 2)]] RWStructuredBuffer<InstanceDataGPU> culledOutputInstances;
[[vk::binding(7, 2)]] RWStructuredBuffer<DrawIndexedIndirectCommand> culledIndirectCommand;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const uint totalInstances = pc.instanceCount;
    if (id.x >= totalInstances)
        return;

    InstanceDataGPU candidate = candidates[id.x];
    const float radius = pc.boundingRadius;
    const float3 boundingCenter = float3(pc.boundingCenterX, pc.boundingCenterY, pc.boundingCenterZ);
    float3 worldPos = candidate.position + boundingCenter;

    float4 viewPos4 = mul(cullCamera.view, float4(worldPos, 1.0));
    float3 viewPos = viewPos4.xyz;
    float dist = -viewPos.z;

    bool visible = true;

    const uint occlusionActive = pc.occlusionEnabled;
    if (occlusionActive != 0 && dist > radius) {
        float nearestViewZ = viewPos.z + radius;

        float4 nearClip = mul(cullCamera.proj, float4(0.0, 0.0, nearestViewZ, 1.0));
        float nearestNdcDepth = nearClip.z / nearClip.w;

        float4 centerClip = mul(cullCamera.proj, viewPos4);
        float invW = 1.0 / max(centerClip.w, 1e-4);
        float2 centerNdc = centerClip.xy * invW;

        float ndcRadiusX = abs(cullCamera.proj[0][0]) * radius / dist;
        float ndcRadiusY = abs(cullCamera.proj[1][1]) * radius / dist;

        float2 uvMin = saturate((centerNdc - float2(ndcRadiusX, ndcRadiusY)) * 0.5 + 0.5);
        float2 uvMax = saturate((centerNdc + float2(ndcRadiusX, ndcRadiusY)) * 0.5 + 0.5);

        const int2 destSize = pc.dstSize;
        float2 boxTexels = (uvMax - uvMin) * float2(destSize);
        float maxBoxTexels = max(boxTexels.x, boxTexels.y);
        
        const uint maximumMip = pc.maxMip;
        float mipLevel = clamp(ceil(log2(max(maxBoxTexels, 1.0))), 0.0, float(maximumMip));

        float d0 = hiZPyramid.SampleLevel(float2(uvMin.x, uvMin.y), mipLevel).r;
        float d1 = hiZPyramid.SampleLevel(float2(uvMax.x, uvMin.y), mipLevel).r;
        float d2 = hiZPyramid.SampleLevel(float2(uvMin.x, uvMax.y), mipLevel).r;
        float d3 = hiZPyramid.SampleLevel(float2(uvMax.x, uvMax.y), mipLevel).r;
        float hiZDepth = max(max(d0, d1), max(d2, d3));

        visible = (nearestNdcDepth <= hiZDepth);
    }

    if (visible) {
        uint slot;
        InterlockedAdd(indirectCommand[0].instanceCount, 1, slot);
        outputInstances[slot] = candidate;
    } else {
        uint culledSlot;
        InterlockedAdd(culledIndirectCommand[0].instanceCount, 1, culledSlot);
        culledOutputInstances[culledSlot] = candidate;
    }
}
