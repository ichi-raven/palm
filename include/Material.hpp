/*****************************************************************/ /**
 * @file   Material.hpp
 * @brief  
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
            ePrinciple,
            eMaterialNum
        };

        struct Params  // std430
        {
            glm::vec4 albedo;
            glm::vec4 emissive;
            int32_t texIndex;
            int32_t materialType;
            float alpha;
            float IOR;
        };

        vk2s::Material materialParam;
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
