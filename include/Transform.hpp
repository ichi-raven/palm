
#ifndef VKPT_INCLUDE_TRANSFORM_HPP_
#define VKPT_INCLUDE_TRANSFORM_HPP_

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

struct Transform
{
    glm::vec3 pos;
    glm::vec3 vel;
    glm::vec3 acc;

    glm::vec3 scale;

    glm::quat rot;

    inline vk::TransformMatrixKHR update(const float deltaTime)
    {
        pos += deltaTime * vel += deltaTime * acc;

        vk::TransformMatrixKHR mtx;
        auto mT = glm::transpose(glm::translate(glm::mat4(1.0), pos) * glm::toMat4(rot) * glm::scale(glm::mat4(1.0), scale));
        memcpy(&mtx.matrix[0], &mT[0], sizeof(float) * 4);
        memcpy(&mtx.matrix[1], &mT[1], sizeof(float) * 4);
        memcpy(&mtx.matrix[2], &mT[2], sizeof(float) * 4);

        return mtx;
    };

    inline vk::TransformMatrixKHR at(const float time)
    {
        glm::vec3 newPos = pos + time * (vel + time * acc); 

        vk::TransformMatrixKHR mtx;
        auto mT = glm::transpose(glm::translate(glm::mat4(1.0), newPos) * glm::toMat4(rot) * glm::scale(glm::mat4(1.0), scale));
        memcpy(&mtx.matrix[0], &mT[0], sizeof(float) * 4);
        memcpy(&mtx.matrix[1], &mT[1], sizeof(float) * 4);
        memcpy(&mtx.matrix[2], &mT[2], sizeof(float) * 4);

        return mtx;
    };
};

#endif