#include "LightTreeData.slangh"

import MortonCodeHelpers;

#ifndef GROUP_SIZE
    // Compile-time error if GROUP_SIZE is not defined.
    #error GROUP_SIZE is not defined. Add define in cpp file.
#endif

cbuffer PerFrameCB
{
    uint workLoad;
    uint srcLevel;
    uint dstLevelStart;
    uint dstLevelEnd;
}

RWStructuredBuffer<LightTreeNode> gLightTree;

[numthreads(GROUP_SIZE, 1, 1)]
void constructLightTree(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    int threadId = dispatchThreadId.x;
    if (threadId >= workLoad) return;

    int nodeOffset = getCurrentLevelNodeStartIdx(dstLevelStart);
    int nodeIdx = nodeOffset + threadId;

    int nodeLevel = uintLog2(nodeIdx + 1);

    int startNodeId = getCurrentLevelNodeStartIdx(srcLevel) + ((nodeIdx - getCurrentLevelNodeStartIdx(nodeLevel)) << (srcLevel - nodeLevel));
    int endNodeId = startNodeId + (1 << (srcLevel - nodeLevel));

    LightTreeNode node = gLightTree[startNodeId];
    node.id = nodeIdx;

    for (int nodeId = startNodeId + 1; nodeId < endNodeId; nodeId++)
    {
        LightTreeNode srcNode = gLightTree[nodeId];
        if (srcNode.isBogus()) continue;

        node.intensity += srcNode.intensity;
        node.aabbMinPoint = min(srcNode.aabbMinPoint, node.aabbMinPoint);
        node.aabbMaxPoint = max(srcNode.aabbMaxPoint, node.aabbMaxPoint);
        node.mortonCode = computeMortonCodeByPos((node.aabbMinPoint + node.aabbMaxPoint) * float3(0.5f));
    }

    node.paddingAndDebug = float4(startNodeId, endNodeId, 0, 0);

    gLightTree[nodeIdx] = node;
}
