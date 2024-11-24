/*****************************************************************/ /**
 * @file   Emitter.hpp
 * @brief  header file of Emitter structs
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
    /**
     * @brief  Struct representing emitter (light source)
     */
    struct Emitter
    {
        /**
         * @brief  Emitter type (must always be kept in sync with shader side)
         */
        enum class Type : int32_t
        {
            ePoint = 0,
            eArea = 1,
            eInfinite = 2,
        };

        /**
         * @brief Emitter parameters (passed to the GPU, must always be kept in sync with shader side)
         */
        struct Params  // std140
        {
            //! Position
            glm::vec3 pos = glm::vec3(0.);
            //! Emitter type
            int32_t type     = static_cast<std::underlying_type_t<Type>>(Type::ePoint);

            //! Number of faces for pdf calculation (only for area emitter)
            uint32_t faceNum = 0;
            //! Index of the Entity's mesh with this Emitter (only for area emitter)
            int32_t meshIndex = -1;
            //! Padding
            float padding[2]  = { 0.f };

            //! The luminous component of this Emitter
            glm::vec3 emissive = glm::vec3(0.);
            //! Index to emissiveTex
            int32_t texIndex = -1;
        };

        //! Parameters
        Params params;
        //! Texture representing the distribution of emissive values
        Handle<vk2s::Image> emissiveTex;
        //! Entity that has this Emitter
        std::optional<ec2s::Entity> attachedEntity;
    };
}  // namespace palm

#endif
