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
    /**
     * @brief  Struct representing mesh 
     */
    struct Mesh
    {
        /**
         * @brief  Vertex type to be used when this mesh is used
         * @detail Follows std140 for use as StructuredBuffer on Integrator side
         */
        struct Vertex
        {
            //! Position
            glm::vec3 pos;
            //! U coordinates of UV
            float u;
            //! Normal vector
            glm::vec3 normal;
            //! V coordinates of UV
            float v;
        };

        //! CPU-side mesh information obtained from vk2s loader
        vk2s::Mesh hostMesh;
        
        Handle<vk2s::Buffer> vertexBuffer;
        Handle<vk2s::Buffer> indexBuffer;

        Handle<vk2s::AccelerationStructure> blas;
        //! Uniform buffer for writing instance information (for rasterization)
        Handle<vk2s::Buffer> instanceBuffer;
    };
}  // namespace palm

#endif
