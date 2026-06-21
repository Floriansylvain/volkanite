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

[[vk::binding(0, 0)]]
Sampler2D fontAtlas;

[shader("fragment")]
float4 fragMainText(TextVSOutput vertIn) : SV_TARGET {
    float coverage = fontAtlas.Sample(vertIn.fragTexCoord).r;
    return float4(vertIn.fragColor, coverage);
}
