#include "Falcor.h"
#include "../HimeUtils.h"

namespace Falcor
{
    struct ShapeData
    {
        Buffer::SharedPtr vertexBuffer;
        Buffer::SharedPtr indexBuffer;
        Vao::SharedPtr vao;
    };

    class HIME_UTILS_DECL Shape
    {
    public:
        virtual ~Shape() = default;

        virtual int getVertexCount() const = 0;
        virtual int getIndexCount() const = 0;
        virtual Vao::SharedPtr getVao() = 0;
        virtual void getInstance(uint& instanceCount, std::vector<float4x4>& instanceMatrixes, Buffer::SharedPtr& pInstanceBuffer);

    protected:
        std::vector<float4x4> mTransformMatrixes;
    };

    class HIME_UTILS_DECL Lines : public Shape
    {
    public:
        Lines() = default;
        Lines(const std::vector<float3>& linePoints) : mPoints(linePoints) {}

        int getVertexCount() const override { return (int)mPoints.size(); }
        int getIndexCount() const override { return -1; }
        Vao::SharedPtr getVao() override;
        void getInstance(uint& instanceCount, std::vector<float4x4>& instanceMatrixes, Buffer::SharedPtr& pInstanceBuffer) override;
        void addInstance(const float3& p1, const float3& p2);

    private:
        std::vector<float3> mPoints;
        Buffer::SharedPtr mpVertexBuffer;
        Vao::SharedPtr mpVao;
    };

    class HIME_UTILS_DECL Cubes : public Shape
    {
    public:
        Cubes() = default;

        int getVertexCount() const override;
        int getIndexCount() const override;
        Vao::SharedPtr getVao() override;
        void addInstance(const float3& minPoint, const float3& maxPoint);
        void addInstance(const float3& scale, const float3& rotation, const float3& translate);

    private:
        static ShapeData mData;
    };

    class HIME_UTILS_DECL WiredCubes : public Cubes
    {
    public:
        WiredCubes() = default;

        int getIndexCount() const override;
        Vao::SharedPtr getVao() override;

    private:
        static ShapeData mData;
    };

    class HIME_UTILS_DECL Spheres : public Shape
    {
    public:
        virtual int getVertexCount() const;
        virtual int getIndexCount() const;
        virtual Vao::SharedPtr getVao();
        void addInstance(const float3& center, const float radius);

    private:
        static ShapeData mData;
    };
}
