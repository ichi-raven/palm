/*****************************************************************/ /**
 * @file   Material.hpp
 * @brief  WARNING: Always synchronized with 
 * 
 * @author ichi-raven
 * @date   October 2024
 *********************************************************************/
#ifndef PALM_INCLUDE_MATERIAL_HPP_
#define PALM_INCLUDE_MATERIAL_HPP_

#include <vk2s/Device.hpp>

#include <glm/glm.hpp>

namespace palm
{
    struct Material
    {
        enum class Type : int32_t
        {
            eLambert = 0,
            ePrinciple = 1,
            eMaterialNum
        };

        struct Params  // std430
        {
            glm::vec3 albedo;
            float roughness;

            float metallic;
            float specTrans;
            float diffTrans;
            float flatness;

            float specularTint;
            glm::vec3 padding;

            float sheen;
            glm::vec3 sheenTint;

            float anisotropic;
            float clearcoat;
            float clearcoatGloss;
            float IOR;

            int32_t albedoTexIndex;
            int32_t roughnessTexIndex;
            int32_t metalnessTexIndex;
            int32_t normalmapTexIndex;

            int32_t materialType;
            glm::vec3 emissive;
        };

        Params materialParams;
        Handle<vk2s::Buffer> uniformBuffer;

        constexpr static uint32_t kDefaultTexNum = 4;
        Handle<vk2s::Image> albedoTex;
        Handle<vk2s::Image> roughnessTex;
        Handle<vk2s::Image> metalnessTex;
        Handle<vk2s::Image> normalMapTex;

        Handle<vk2s::BindGroup> bindGroup;
    };
}  // namespace palm

#endif
