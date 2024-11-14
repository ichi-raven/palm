/*****************************************************************/ /**
 * @file   Emitter.hpp
 * @brief  
 * 
 * @author ichi-raven
 * @date   November 2024
 *********************************************************************/
#ifndef PALM_INCLUDE_EMITTER_HPP_
#define PALM_INCLUDE_EMITTER_HPP_

#include <vk2s/Device.hpp>
#include <EC2S.hpp>
#include <glm/glm.hpp>

#include <utility>

namespace palm
{
    struct Emitter
    {
        enum class Type : int32_t
        {
            ePoint = 0,
            eArea = 1,
            eInfinite = 2,
        };

        struct Params  // std140
        {
            glm::vec3 pos = glm::vec3(0.);
            int32_t type     = static_cast<std::underlying_type_t<Type>>(Type::ePoint);

            // for area emitter
            uint32_t faceNum = 0;
            int32_t meshIndex = -1;
            float padding[2];

            glm::vec3 emissive = glm::vec3(0.);
            int32_t texIndex = -1;
        };

        Params params;
        Handle<vk2s::Image> emissiveTex;
        std::optional<ec2s::Entity> attachedEntity;
    };
}  // namespace palm

#endif
