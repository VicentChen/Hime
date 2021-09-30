#include "VisualizeShape.h"

using namespace Falcor;

namespace
{
    const char kShaderFile[] = "RenderPasses/Hime/HimeUtils/Shape/VisualizeShape.3d.slang";
    const char kVertexShaderEntryPoint[] = "vsMain";
    const char kPixelShaderEntryPoint[] = "psMain";
}

ShapeVisualizer::SharedPtr ShapeVisualizer::create(RenderContext* pRenderContext, const Dictionary& dict)
{
    return SharedPtr(new ShapeVisualizer());
}

void ShapeVisualizer::drawLines(RenderContext* pContext, Lines& lines, const float3& color, Texture::SharedPtr& pTexture, const Camera::SharedPtr& pCamera, Texture::SharedPtr& pPositionTexture)
{
    draw(pContext, &lines, RasterizerState::FillMode::Solid, RasterizerState::CullMode::None, false, color, pTexture, pCamera, pPositionTexture);
}

void ShapeVisualizer::drawCubes(RenderContext* pContext, Cubes& cubes, const float3& color, Texture::SharedPtr& pTexture, const Camera::SharedPtr& pCamera, Texture::SharedPtr& pPositionTexture)
{
    draw(pContext, &cubes, RasterizerState::FillMode::Solid, RasterizerState::CullMode::None, false, color, pTexture, pCamera, pPositionTexture);
}

void ShapeVisualizer::drawWiredCubes(RenderContext* pContext, WiredCubes& wiredCubes, const float3& color, Texture::SharedPtr& pTexture, const Camera::SharedPtr& pCamera, Texture::SharedPtr& pPositionTexture)
{
    draw(pContext, &wiredCubes, RasterizerState::FillMode::Solid, RasterizerState::CullMode::None, false, color, pTexture, pCamera, pPositionTexture);
}

void ShapeVisualizer::drawSpheres(RenderContext* pContext, Spheres& spheres, const float3& color, Texture::SharedPtr& pTexture, const Camera::SharedPtr& pCamera, Texture::SharedPtr& pPositionTexture)
{
    draw(pContext, &spheres, RasterizerState::FillMode::Solid, RasterizerState::CullMode::None, false, color, pTexture, pCamera, pPositionTexture);
}

void ShapeVisualizer::drawWiredSpheres(RenderContext* pContext, Spheres& spheres, const float3& color, Texture::SharedPtr& pTexture, const Camera::SharedPtr& pCamera, Texture::SharedPtr& pPositionTexture)
{
    draw(pContext, &spheres, RasterizerState::FillMode::Wireframe, RasterizerState::CullMode::None, false, color, pTexture, pCamera, pPositionTexture);
}

ShapeVisualizer::ShapeVisualizer()
{
    mpVisualizePass = RasterPass::create(kShaderFile, kVertexShaderEntryPoint, kPixelShaderEntryPoint);
}

void ShapeVisualizer::draw(RenderContext* pContext, Shape* pShape, RasterizerState::FillMode fillMode, RasterizerState::CullMode cullMode, bool enableDepth, const float3& color, Texture::SharedPtr& pTexture, const Camera::SharedPtr& pCamera, Texture::SharedPtr& pPositionTexture)
{
    PROFILE("Visualize Shapes");

    Fbo::SharedPtr pFbo = Fbo::create({ pTexture });

    RasterizerState::Desc rasterizeStateDesc;
    rasterizeStateDesc.setFillMode(fillMode);
    rasterizeStateDesc.setCullMode(cullMode);

    RasterizerState::SharedPtr pRasterizeState = RasterizerState::create(rasterizeStateDesc);

    DepthStencilState::Desc depthStencilStateDesc;
    depthStencilStateDesc.setDepthEnabled(enableDepth);

    DepthStencilState::SharedPtr pDepthStencilState = DepthStencilState::create(depthStencilStateDesc);

    auto& pVars = mpVisualizePass->getVars();
    auto& pState = mpVisualizePass->getState();
    pState->setFbo(pFbo);
    pState->setVao(pShape->getVao());
    pState->setRasterizerState(pRasterizeState);
    pState->setDepthStencilState(pDepthStencilState);

    uint instanceCount;
    std::vector<float4x4> instanceMatrixes;
    Buffer::SharedPtr pInstanceBuffer;

    pShape->getInstance(instanceCount, instanceMatrixes, pInstanceBuffer);

    mpVisualizePass->getRootVar()["PerFrameCB"]["color"] = color;
    mpVisualizePass->getRootVar()["PerFrameCB"]["camPos"] = pCamera->getPosition();
    mpVisualizePass->getRootVar()["PerFrameCB"]["viewMat"] = pCamera->getViewMatrix();
    mpVisualizePass->getRootVar()["PerFrameCB"]["projMat"] = pCamera->getProjMatrix();
    mpVisualizePass->getRootVar()["gInstanceWorldMats"] = pInstanceBuffer;
    mpVisualizePass->getRootVar()["gPosW"] = pPositionTexture;

    int vertexCount = pShape->getVertexCount();
    int indexCount = pShape->getIndexCount();

    if (indexCount < 0) // indexCount < 0 means this shape has no index buffer.
    {
        // No instance count needed here.
        mpVisualizePass->draw(pContext, vertexCount, 0);
    }
    else
    {
        pContext->drawIndexedInstanced(mpVisualizePass->getState().get(), mpVisualizePass->getVars().get(), indexCount, instanceCount, 0, 0, 0);
    }
}
