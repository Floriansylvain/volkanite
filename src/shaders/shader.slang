struct VSInput {
    float3 inPos;
    float3 inColor;
    float2 inTexCoord;
};

struct InstancedVSInput {
    float3 inPos;
    float3 inColor;
    float2 inTexCoord;
    float3 instancePos;
    float instanceRotation;
};

struct CameraUniform {
    float4x4 view;
    float4x4 proj;
};
ConstantBuffer<CameraUniform> ubo;

struct PushConstants {
    float4x4 model;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

struct VSOutput {
    float4 pos : SV_Position;
    float3 fragColor;
    float2 fragTexCoord;
};

float4x4 rotationMatrix(float3 axis, float angle) {
    axis = normalize(axis);
    float s = sin(angle);
    float c = cos(angle);
    float oc = 1.0 - c;

    float4x4 m;
    m[0] = float4(oc*axis.x*axis.x + c,        oc*axis.x*axis.y - axis.z*s, oc*axis.z*axis.x + axis.y*s, 0.0);
    m[1] = float4(oc*axis.x*axis.y + axis.z*s, oc*axis.y*axis.y + c,        oc*axis.y*axis.z - axis.x*s, 0.0);
    m[2] = float4(oc*axis.z*axis.x - axis.y*s, oc*axis.y*axis.z + axis.x*s, oc*axis.z*axis.z + c,        0.0);
    m[3] = float4(0.0, 0.0, 0.0, 1.0);
    return m;
}

[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput output;
    output.pos = mul(ubo.proj, mul(ubo.view, mul(pc.model, float4(input.inPos, 1.0))));
    output.fragColor = input.inColor;
    output.fragTexCoord = input.inTexCoord;
    return output;
}

[shader("vertex")]
VSOutput vertMainInstanced(InstancedVSInput input) {
    VSOutput output;
    float4x4 rot = rotationMatrix(float3(1.0, 0.0, 1.0), input.instanceRotation);
    float4 rotatedPos = mul(rot, float4(input.inPos, 1.0));
    float4 worldPos = float4(rotatedPos.xyz + input.instancePos, 1.0);
    output.pos = mul(ubo.proj, mul(ubo.view, worldPos));
    output.fragColor = input.inColor;
    output.fragTexCoord = input.inTexCoord;
    return output;
}

Sampler2D texture;

[shader("fragment")]
float4 fragMain(VSOutput vertIn) : SV_TARGET {
   return float4(vertIn.fragColor * texture.Sample(vertIn.fragTexCoord).rgb, 1.0);
}
