struct TextVSInput {
    float2 inPos;
    float3 inColor;
    float2 inTexCoord;
};

struct TextVSOutput {
    float4 pos : SV_Position;
    float3 fragColor;
    float2 fragTexCoord;
};

[shader("vertex")]
TextVSOutput vertMainText(TextVSInput input) {
    TextVSOutput output;
    output.pos = float4(input.inPos, 0.0, 1.0);
    output.fragColor = input.inColor;
    output.fragTexCoord = input.inTexCoord;
    return output;
}

[[vk::binding(0, 0)]] Texture2D fontTexture;
[[vk::binding(1, 0)]] SamplerState fontSampler;

[shader("fragment")]
float4 fragMainText(TextVSOutput vertIn) : SV_TARGET {
    uint width, height;
    fontTexture.GetDimensions(width, height);
    float2 texelSize = float2(1.0 / width, 1.0 / height);

    float centerCoverage = fontTexture.Sample(fontSampler, vertIn.fragTexCoord).r;
    float maxNeighbor = 0.0;

    const float2 directions[8] = {
        float2(1.0, 0.0), float2(-1.0, 0.0), float2(0.0, 1.0), float2(0.0, -1.0),
        float2(0.707, 0.707), float2(-0.707, 0.707), float2(0.707, -0.707), float2(-0.707, -0.707)
    };

    [unroll]
    for (int i = 0; i < 8; i++) {
        float2 offsetMid = directions[i] * 3.0 * texelSize;
        float2 offsetMax = directions[i] * 6.0 * texelSize;
        
        maxNeighbor = max(maxNeighbor, fontTexture.Sample(fontSampler, vertIn.fragTexCoord + offsetMid).r);
        maxNeighbor = max(maxNeighbor, fontTexture.Sample(fontSampler, vertIn.fragTexCoord + offsetMax).r);
    }

    float outlineCoverage = max(maxNeighbor, centerCoverage);
    float3 outlineColor = float3(0.0, 0.0, 0.0);
    float3 textColor = vertIn.fragColor;
    float3 finalColor = lerp(outlineColor, textColor, centerCoverage);

    return float4(finalColor, outlineCoverage);
}
