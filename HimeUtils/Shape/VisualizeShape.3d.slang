struct VSIn
{
    float3 posW : POSITION;
};

struct VSOut
{
    float3 posW : POSITION;
    float4 posH : SV_POSITION;
};

cbuffer PerFrameCB
{
    float3   color;
    float3   camPos;
    float4x4 viewMat;
    float4x4 projMat;
}

Texture2D<float4> gPosW;
StructuredBuffer<float4x4> gInstanceWorldMats;

VSOut vsMain(VSIn vsIn, uint instanceID : SV_InstanceID)
{
    uint instanceMatIdx = instanceID;
    float4x4 worldMat = gInstanceWorldMats[instanceMatIdx];
    float4x4 worldViewProj = mul(mul(worldMat, viewMat), projMat);
    float4 posW = mul(float4(vsIn.posW, 1.0f), worldMat);
    
    VSOut vsOut;
    float4 posH = mul(float4(vsIn.posW, 1.0f), worldViewProj);
    vsOut.posW = posW.xyz;
    vsOut.posH = posH;
    return vsOut;
}

float4 psMain(VSOut vsOut) : SV_TARGET
{
    float4 wireColor = float4(color, 1);
    float3 posW = gPosW[vsOut.posH.xy].xyz;

    if (distance(vsOut.posW, camPos) < distance(posW, camPos)) return wireColor;
    else return float4(0, 0, 0, 1);
}
