struct InstancedVSInput {
    float3 inPos;
    float3 inColor;
    float2 inTexCoord;
    float3 inNormal;
    float3 inTangent;
    float3 instancePos;
    float3 instanceRotation;
};

struct CameraUniform {
    float4x4 view;
    float4x4 proj;
    float4 cameraPos;
};

[[vk::binding(0, 0)]] ConstantBuffer<CameraUniform> ubo;

struct VSOutput {
    float4 pos : SV_Position;
    float3 fragColor;
    float2 fragTexCoord;
    float3 fragNormal;
    float3 fragTangent;
    float3 worldPos;
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

float4x4 eulerRotationMatrix(float3 angles) {
    float4x4 rx = rotationMatrix(float3(1.0, 0.0, 0.0), angles.x);
    float4x4 ry = rotationMatrix(float3(0.0, 1.0, 0.0), angles.y);
    float4x4 rz = rotationMatrix(float3(0.0, 0.0, 1.0), angles.z);
    return mul(rz, mul(ry, rx));
}

static const float PI = 3.14159265359;

float distributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float geometrySchlickGGX(float NdotX, float k) {
    return NdotX / (NdotX * (1.0 - k) + k);
}

float geometrySmith(float NdotV, float NdotL, float roughness) {
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    return geometrySchlickGGX(NdotV, k) * geometrySchlickGGX(NdotL, k);
}

float3 fresnelSchlick(float VdotH, float3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
}

[shader("vertex")]
VSOutput vertMainInstanced(InstancedVSInput input) {
    VSOutput output;
    float4x4 rot = eulerRotationMatrix(input.instanceRotation);
    float4 rotatedPos = mul(rot, float4(input.inPos, 1.0));
    float4 worldPos = float4(rotatedPos.xyz + input.instancePos, 1.0);

    output.pos = mul(ubo.proj, mul(ubo.view, worldPos));
    float3x3 rot3 = (float3x3)rot;
    output.fragNormal = normalize(mul(rot3, input.inNormal));
    output.fragTangent = normalize(mul(rot3, input.inTangent));
    output.fragColor = input.inColor;
    output.fragTexCoord = input.inTexCoord;
    output.worldPos = worldPos.xyz;

    return output;
}

[[vk::binding(1, 0)]] Sampler2D texture;
[[vk::binding(0, 1)]] Sampler2D normalMap;

[shader("fragment")]
float4 fragMain(VSOutput vertIn) : SV_TARGET {
    float4 texColor = texture.Sample(vertIn.fragTexCoord);
    if (texColor.a < 0.5)
        discard;

    float3 N = normalize(vertIn.fragNormal);
    float3 T = normalize(vertIn.fragTangent);
    T = normalize(T - N * dot(N, T));
    float3 B = cross(N, T);
    float3x3 TBN = float3x3(T, B, N);

    float3 tangentNormal = normalMap.Sample(vertIn.fragTexCoord).rgb * 2.0 - 1.0;
    N = normalize(mul(tangentNormal, TBN));

    float3 L = normalize(float3(1.0, 1.0, 1.0));
    float3 V = normalize(ubo.cameraPos.xyz - vertIn.worldPos);
    float3 H = normalize(L + V);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float LdotH = max(dot(L, H), 0.0);
    float NdotH = max(dot(N, H), 0.0);

    float roughness = 0.5;

    float fd90 = 0.5 + 2.0 * roughness * LdotH * LdotH;
    float lightScatter = 1.0 + (fd90 - 1.0) * pow(1.0 - NdotL, 5.0);
    float viewScatter = 1.0 + (fd90 - 1.0) * pow(1.0 - NdotV, 5.0);

    float burleyDiffuse = lightScatter * viewScatter * NdotL;

    const float3 Fzero = float3(0.04, 0.04, 0.04);
    float D = distributionGGX(NdotH, roughness);
    float G = geometrySmith(NdotV, NdotL, roughness);
    float3 F = fresnelSchlick(max(dot(V, H), 0.0), Fzero);

    float3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);

    float ambient = 0.25;
    float lightingFactor = burleyDiffuse + ambient;

    float3 finalColor = texColor.rgb * lightingFactor + specular * NdotL;

    return float4(finalColor, 1.0);
}

[shader("fragment")]
float4 fragMainXray(VSOutput vertIn) : SV_TARGET {
    return float4(1.0, 0.0, 0.0, 0.35);
}
