cbuffer PerObject : register(b0) {
    float4x4 mvpMatrix; // pre-transposed on CPU (row-major -> column-major)
    float4   tintColor;
};

struct VSInput {
    float3 position : POSITION;
    float4 color    : COLOR;
    float2 texCoord : TEXCOORD;
};

struct VSOutput {
    float4 position : SV_Position;
    float4 color    : COLOR;
    float2 uv       : TEXCOORD;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    output.position = mul(mvpMatrix, float4(input.position, 1.0));
    output.color    = input.color * tintColor;
    output.uv       = input.texCoord;
    return output;
}
