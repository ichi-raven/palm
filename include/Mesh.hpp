/*****************************************************************/ /**
 * @file   Mesh.hpp
 * @brief  
 * 
 * @author ichi-raven
 * @date   October 2024
 *********************************************************************/
#ifndef PALM_INCLUDE_MESH_HPP_
#define PALM_INCLUDE_MESH_HPP_

#include <vk2s/Device.hpp>
#include <vk2s/Scene.hpp>

namespace palm
{
    struct Mesh
    {
        struct Vertex
        {
            glm::vec3 pos;
            float u;
            glm::vec3 normal;
            float v;
        };

        vk2s::Mesh hostMesh;
        Handle<vk2s::Buffer> vertexBuffer;
        Handle<vk2s::Buffer> indexBuffer;

        Handle<vk2s::AccelerationStructure> blas;
        Handle<vk2s::Buffer> instanceBuffer;
    };
}  // namespace palm

#endif
