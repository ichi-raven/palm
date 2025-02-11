/*****************************************************************//**
 * @file   Transform.hpp
 * @brief  
 * 
 * @author ichi-raven
 * @date   October 2024
 *********************************************************************/
#ifndef PALM_INCLUDE_TRANSFORM_HPP_
#define PALM_INCLUDE_TRANSFORM_HPP_

#include <EC2S.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vulkan/vulkan.hpp>

#include <vk2s/Device.hpp>

namespace palm
{
    /**
     * @brief  Struct representing instance transforms
     */
    struct Transform
    {
        /**
         * @brief Material parameters (passed to the GPU, must always be kept in sync with shader side)
         */
        struct Params
        {
            //! World matrix (model matrix)
            glm::mat4 world = glm::identity<glm::mat4>();
            //! The inverse transpose of the world matrix (transpose(inverse(world matrix)))
            glm::mat4 worldInvTranspose = glm::identity<glm::mat4>();
            //! Velocity (difference in position from the previous frame)
            glm::vec3 vel = glm::vec3(0.0);
            //! Slot part of Entity with this Transform
            uint32_t entitySlot = 0;
            //! Padding
            glm::vec3 padding = glm::vec3(0.0);
            //! Index part of Entity with this Transform
            uint32_t entityIndex = 0;

            /** 
             * @brief  Update each parameter/matrix from the TRS vector
             *  
             * @param translate translate vector
             * @param rotation rotation vector
             * @param scaling scaling vector
             */
            void update(glm::vec3 translate, const glm::quat& rotation, const glm::vec3& scaling)
            {
                vel = translate - glm::vec3(world[0][3], world[1][3], world[2][3]);

                world             = glm::translate(glm::identity<glm::mat4>(), translate) * glm::mat4_cast(rotation) * glm::scale(glm::identity<glm::mat4>(), scaling);
                worldInvTranspose = glm::transpose(glm::inverse(world));
            }

            /** 
             * @brief  Convert the current world matrix to Vulkan's matrix representation for AS
             *  
             * @return World matrix (in Vulkan format)
             */
            vk::TransformMatrixKHR convert() const
            {
                vk::TransformMatrixKHR ret;
                const auto mT = glm::transpose(world);
                memcpy(&ret.matrix[0], &mT[0], sizeof(float) * 4);
                memcpy(&ret.matrix[1], &mT[1], sizeof(float) * 4);
                memcpy(&ret.matrix[2], &mT[2], sizeof(float) * 4);

                return ret;
            };
        };

        //! Parameter
        Params params;
        //! Position (translate) vector
        glm::vec3 pos   = { 0.f, 0.f, 0.f };
        //! Rotation quaternion
        glm::quat rot   = { 1.f, 0.f, 0.f, 0.f };
        //! Scale vector
        glm::vec3 scale = { 1.f, 1.f, 1.f };
        //! Uniform buffer to write Params
        Handle<vk2s::DynamicBuffer> uniformBuffer;
        //! BindGroup for entityBuffer (only for rasterize)
        Handle<vk2s::BindGroup> bindGroup;
    };
}

#endif
