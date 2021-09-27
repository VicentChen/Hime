import Scene.ShadingData;

struct VsOut
{
    float2 texC       : TEXCOORD;
#ifndef _VIEWPORT_MASK
    float4 posH       : SV_POSITION;
#else
    float4 posH       : POSITION;
#endif
};

cbuffer PerFrameCB
{
    float gCPhi;
    float gNPhi;
    float gPPhi;
    int gStepSize;

    float2 gResolution;
};

Texture2D<float4> gColorMap;
Texture2D<float4> gNormalMap;
Texture2D<float4> gPosMap;

float computeEdgeStoppingWeight(float4 pVal, float4 qVal, float phi)
{
    float4 t = pVal - qVal;
    float dist2 = dot(t, t);
    float w = exp(-(dist2) / phi);
    return w;
}

float4 main(VsOut vsOut) : SV_TARGET0
{
    const int2 ipos = int2(vsOut.posH.xy);
    
    const float kernel[3] = { 3.0 / 8.0, 1.0 / 4.0, 1.0 / 16.0 };
    
    float4 sum = float4(0.0);
    float2 step = 1.0f / gResolution;
    float4 cVal = gColorMap[ipos];
    float4 nVal = gNormalMap[ipos];
    float4 pVal = gPosMap[ipos];
    float cumW = 0.0;

    for (int yy = -2; yy <= 2; yy++)
    {
        for (int xx = -2; xx <= 2; xx++)
        {
            const int2 uv = ipos + int2(xx, yy) * gStepSize;
            const bool inside = all(uv >= int2(0,0)) && all(uv < gResolution);

            if (inside)
            {
                float4 cTmp = gColorMap[uv];
                float4 nTmp = gNormalMap[uv];
                float4 pTmp = gPosMap[uv];
                float cW = computeEdgeStoppingWeight(cVal, cTmp, gCPhi);
                float nW = computeEdgeStoppingWeight(nVal, nTmp, gNPhi);
                float pW = computeEdgeStoppingWeight(pVal, pTmp, gPPhi);

                float kernelVal = kernel[abs(xx)] * kernel[abs(yy)];
            
                float weight = cW * nW * pW;
                sum += cTmp * weight * kernelVal;
                cumW += weight * kernelVal;
            }
        }
    }
    
    return sum / cumW;
}
