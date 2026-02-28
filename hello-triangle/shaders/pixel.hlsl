Texture2D    gAlbedo  : register(t0);
SamplerState gSampler : register(s0);

struct PSInput {
    float4 position : SV_Position;
    float4 color    : COLOR;
    float2 uv       : TEXCOORD;
};

float4 PSMain(PSInput input) : SV_Target {
    // Texture color multiplied by vertex color (white by default -> no tint).
    return gAlbedo.Sample(gSampler, input.uv) * input.color;
}
