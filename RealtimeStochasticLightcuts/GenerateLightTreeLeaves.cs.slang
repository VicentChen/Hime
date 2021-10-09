#include "LightTreeData.slangh"
import Scene.Scene;
import Scene.Lights.LightCollection;
import HimeUtils.HimeMortonCode;

#ifndef GROUP_SIZE
    // Compile-time error if GROUP_SIZE is not defined.
    #error GROUP_SIZE is not defined. Add define in cpp file.
#endif

cbuffer PerFrameCB
{
    uint lightCount = 0;
    uint levelCount = 0;
}

RWStructuredBuffer<LightTreeNode> gLightTree;
RWStructuredBuffer<LightTreeNode> gSortingHelper;
RWByteAddressBuffer gSortingKeyIndex;

[numthreads(GROUP_SIZE, 1, 1)]
void generateLightTreeLeaves(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint threadId = dispatchThreadId.x;
    uint nodeOffset = getCurrentLevelNodeStartIdx(levelCount - 1);
    uint nodeIdx = nodeOffset + threadId;

    if (threadId >= lightCount)
    {
        // bogus light calculation
        LightTreeNode node = {};
        node.id = nodeIdx;
        node.lightIdx = 0;
        node.mortonCode = 0xFFFFFFFF;
        node.intensity = float3(0);
        node.aabbMinPoint = float3(100000000.f);
        node.aabbMaxPoint = float3(100000000.f);
        node.paddingAndDebug = float4(0);
        gLightTree[nodeIdx] = node;

        return;
    }

    // prepare triangle
    const EmissiveTriangle tri = gScene.lightCollection.getTriangle(threadId);
    float3 barycentrics = float3(1.0f / 3);
    float3 triangleCenter = tri.getPosition(barycentrics);
    float2 uv = tri.getTexCoord(barycentrics);
    float3 emissive = gScene.getEmissive(tri.materialID, uv, gScene.materialResources[tri.materialID]);
    float area = tri.area;

    uint mortonCode = computeMortonCodeByPos(triangleCenter);

    LightTreeNode node = {};
    node.id = nodeIdx;
    node.lightIdx = threadId;
    node.mortonCode = mortonCode;
    node.intensity = emissive * area;
    node.aabbMinPoint = float3(triangleCenter);
    node.aabbMaxPoint = float3(triangleCenter);
    node.paddingAndDebug = float4(0);
    gLightTree[nodeIdx] = node;
    gSortingHelper[threadId] = node;
    gSortingKeyIndex.Store2(8 * threadId, uint2(threadId, node.mortonCode));
}
