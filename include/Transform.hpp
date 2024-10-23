/*****************************************************************//**
 * @file   Transform.hpp
 * @brief  
 * 
 * @author ichi-raven
 * @date   October 2024
 *********************************************************************/
#ifndef PALM_INCLUDE_TRANSFORM_HPP_
#define PALM_INCLUDE_TRANSFORM_HPP_

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <vulkan/vulkan.hpp>

#include <vk2s/Device.hpp>

namespace palm
{
    struct Transform
    {
        struct Params
        {
            glm::mat4 model;
            glm::mat4 modelInvTranspose;
            glm::vec3 vel;
            float padding;

            void update(glm::vec3 translate, const glm::vec3& rotation, const glm::vec3& scaling)
            {
                vel = translate - glm::vec3(model[0][3], model[1][3], model[2][3]);

                model             = glm::translate(glm::mat4(1.f), translate) * glm::toMat4(glm::quat(rotation)) * glm::scale(glm::mat4(1.f), scaling);
                modelInvTranspose = glm::transpose(glm::inverse(model));
            }

            vk::TransformMatrixKHR convert() const
            {
                vk::TransformMatrixKHR mtx;
                auto mT = glm::transpose(model);
                memcpy(&mtx.matrix[0], &mT[0], sizeof(float) * 4);
                memcpy(&mtx.matrix[1], &mT[1], sizeof(float) * 4);
                memcpy(&mtx.matrix[2], &mT[2], sizeof(float) * 4);

                return mtx;
            };
        };

        Params params;
        glm::vec3 pos   = { 0.f, 0.f, 0.f };
        glm::vec3 rot   = { 0.f, 0.f, 0.f };
        glm::vec3 scale = { 1.f, 1.f, 1.f };
        Handle<vk2s::DynamicBuffer> entityBuffer;
        Handle<vk2s::BindGroup> bindGroup;
    };
}

#endif
