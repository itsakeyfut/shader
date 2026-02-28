Texture2D    gAlbedo  : register(t0);
SamplerState gSampler : register(s0);

cbuffer PerFrame : register(b1) {
    float  time;       // accumulated time in seconds
    float  deltaTime;  // last frame duration in seconds
    float2 padding;    // explicit padding to satisfy 16-byte cbuffer alignment
};

struct PSInput {
    float4 position : SV_Position;
    float4 color    : COLOR;
    float2 uv       : TEXCOORD;
};

float4 PSMain(PSInput input) : SV_Target {
    // Scroll UV horizontally at 0.1 units/second.
    // WRAP sampler tiles the checkerboard seamlessly across [0, inf).
    float2 animUV = input.uv + float2(time * 0.1, 0.0);
    return gAlbedo.Sample(gSampler, animUV) * input.color;
}
