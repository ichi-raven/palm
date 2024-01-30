#ifndef VKPT_INCLUDE_SCENE_HPP_
#define VKPT_INCLUDE_SCENE_HPP_

#include <cstdint>

#include <glm/glm.hpp>

#include <vk2s/Device.hpp>
#include <vk2s/AssetLoader.hpp>
#include <vk2s/Camera.hpp>

#include "Transform.hpp"

namespace vkpt
{
    struct Mesh
    {
        vk2s::AssetLoader::Mesh hostMesh;
        Handle<vk2s::Buffer> vertexBuffer;
        Handle<vk2s::Buffer> indexBuffer;

        Handle<vk2s::AccelerationStructure> blas;

        void build(vk2s::Device& device)
        {
            {  // vertex buffer
                const auto vbSize  = hostMesh.vertices.size() * sizeof(vk2s::AssetLoader::Vertex);
                const auto vbUsage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eStorageBuffer;
                vk::BufferCreateInfo ci({}, vbSize, vbUsage);
                vk::MemoryPropertyFlags fb = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;

                vertexBuffer = device.create<vk2s::Buffer>(ci, fb);
                vertexBuffer->write(hostMesh.vertices.data(), vbSize);
            }

            {  // index buffer

                const auto ibSize  = hostMesh.indices.size() * sizeof(uint32_t);
                const auto ibUsage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eStorageBuffer;

                vk::BufferCreateInfo ci({}, ibSize, ibUsage);
                vk::MemoryPropertyFlags fb = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;

                indexBuffer = device.create<vk2s::Buffer>(ci, fb);
                indexBuffer->write(hostMesh.indices.data(), ibSize);
            }

            {// BLAS
                blas = device.create<vk2s::AccelerationStructure>(hostMesh.vertices.size(), sizeof(vk2s::AssetLoader::Vertex), vertexBuffer.get(), hostMesh.indices.size() / 3, indexBuffer.get());
            }
        }
    };

    struct Instance
    {
        Mesh mesh;
        uint32_t materialIndex;
        Transform transform;
    };


    //template<typename Instance, typename Material, typename Camera>
    //class Scene
    //{
    //public:
    //    Scene();

    //    void build(vk2s::Device& device);

    //    Handle<vk2s::Buffer> getMappingBuffer() const;

    //private:
    //    struct InstanceMapping  // std430
    //    {
    //        uint64_t VBAddress;
    //        uint64_t IBAddress;
    //        uint32_t materialIndex;
    //        uint32_t padding[3];
    //    };

    //private:
    //    inline vk::TransformMatrixKHR convert(const glm::mat4x3& m)
    //    {
    //        vk::TransformMatrixKHR mtx;
    //        auto mT = glm::transpose(m);
    //        memcpy(&mtx.matrix[0], &mT[0], sizeof(float) * 4);
    //        memcpy(&mtx.matrix[1], &mT[1], sizeof(float) * 4);
    //        memcpy(&mtx.matrix[2], &mT[2], sizeof(float) * 4);

    //        return mtx;
    //    };

    //private:
    //    std::vector<Instance> mInstances;

    //    Handle<vk2s::Buffer> mInstanceMapBuffer;
    //};
}  // namespace vkpt

#endif