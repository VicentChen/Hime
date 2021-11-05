import Scene.Scene;
import Scene.ShadingData;
import RenderPasses.Shared.PathTracer.LoadShadingData;

#ifndef CHUNK_SIZE
    // Compile-time error if CHUNK_SIZE is not defined.
    #error CHUNK_SIZE is not defined. Add define in cpp file.
#endif

cbuffer PerFrameCB
{
    // Compute shader parameters
    uint2 dispatchDim;
}

Texture2D<float2> gLinearZAndDeriv;
RWTexture2D<float4> gPrevNormalAndLinearZ;

[numthreads(CHUNK_SIZE, CHUNK_SIZE, 1)]
void computeNormalAndLinearZ(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    int2 launchIdx = dispatchThreadId.xy;
    int2 launchDim = dispatchDim.xy;
    if (any(launchIdx >= launchDim)) return;
    
    float4 normalAndLinearZ = float4(0, 0, 0, 0);

    ShadingData sd;
    if (loadShadingData(launchIdx, launchDim, gScene.camera, sd))
    {
        normalAndLinearZ.xyz = sd.N;
        normalAndLinearZ.w   = gLinearZAndDeriv[launchIdx].x;
    }
    gPrevNormalAndLinearZ[launchIdx] = normalAndLinearZ;
}
