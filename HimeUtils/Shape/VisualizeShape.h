#pragma once
#include "Falcor.h"
#include "Shape.h"

namespace Falcor
{
    class HIME_UTILS_DECL ShapeVisualizer
    {
    public:
        using SharedPtr = std::shared_ptr<ShapeVisualizer>;
        static SharedPtr create(RenderContext* pRenderContext = nullptr, const Dictionary& dict = {});

        void drawLines(RenderContext* pContext, Lines& lines, const float3& color, Texture::SharedPtr& pTexture, const Camera::SharedPtr& pCamera, Texture::SharedPtr& pPositionTexture);
        void drawCubes(RenderContext* pContext, Cubes& cubes, const float3& color, Texture::SharedPtr& pTexture, const Camera::SharedPtr& pCamera, Texture::SharedPtr& pPositionTexture);
        void drawWiredCubes(RenderContext* pContext, WiredCubes& wiredCubes, const float3& color, Texture::SharedPtr& pTexture, const Camera::SharedPtr& pCamera, Texture::SharedPtr& pPositionTexture);
        void drawSpheres(RenderContext* pContext, Spheres& spheres, const float3& color, Texture::SharedPtr& pTexture, const Camera::SharedPtr& pCamera, Texture::SharedPtr& pPositionTexture);
        void drawWiredSpheres(RenderContext* pContext, Spheres& spheres, const float3& color, Texture::SharedPtr& pTexture, const Camera::SharedPtr& pCamera, Texture::SharedPtr& pPositionTexture);

    private:
        ShapeVisualizer();
        void draw(RenderContext* pContext, Shape* pShape, RasterizerState::FillMode fillMode, RasterizerState::CullMode cullMode, bool enableDepth, const float3& color, Texture::SharedPtr& pTexture, const Camera::SharedPtr& pCamera, Texture::SharedPtr& pPositionTexture);

        RasterPass::SharedPtr mpVisualizePass;
    };
}
