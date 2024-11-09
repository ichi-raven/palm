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

#include <array>
#include <utility>

namespace palm
{
    struct Material
    {
        constexpr static std::array kMaterialTypesStr = { "Lambert", "Principle" };

        enum class Type : int32_t
        {
            eLambert   = 0,
            ePrinciple = 1,
            eMaterialNum
        };

        struct Params  // std140
        {
            glm::vec3 albedo;
            float roughness;

            float metallic;
            float specTrans;
            float diffTrans;
            float flatness;

            glm::vec3 padding;
            float specularTint;

            glm::vec3 sheenTint;
            float sheen;

            float anisotropic;
            float clearcoat;
            float clearcoatGloss;
            float IOR = 1.0;

            int32_t albedoTexIndex = -1;
            int32_t roughnessTexIndex = -1;
            int32_t metalnessTexIndex = -1;
            int32_t normalmapTexIndex = -1;

            glm::vec3 emissive;
            int32_t materialType = static_cast<std::underlying_type_t<Type>>(Type::eLambert);
        };

        inline static void updateAndDrawMaterialUI(Params& params, bool& enabledEmissive)
        {
            // Albedo color
            ImGui::ColorEdit3("Albedo", glm::value_ptr(params.albedo), ImGuiColorEditFlags_Float);

            // Roughness slider
            ImGui::SliderFloat("Roughness", &params.roughness, 0.0f, 1.0f);

            // Metallic slider
            ImGui::SliderFloat("Metallic", &params.metallic, 0.0f, 1.0f);

            // Specular transmission
            ImGui::SliderFloat("Specular Transmission", &params.specTrans, 0.0f, 1.0f);

            // Diffuse transmission
            ImGui::SliderFloat("Diffuse Transmission", &params.diffTrans, 0.0f, 1.0f);

            // Flatness
            ImGui::SliderFloat("Flatness", &params.flatness, 0.0f, 1.0f);

            // Specular tint
            ImGui::SliderFloat("Specular Tint", &params.specularTint, 0.0f, 1.0f);

            // Sheen
            ImGui::SliderFloat("Sheen", &params.sheen, 0.0f, 1.0f);

            // Sheen tint color
            ImGui::ColorEdit3("Sheen Tint", glm::value_ptr(params.sheenTint), ImGuiColorEditFlags_Float);

            // Anisotropic
            ImGui::SliderFloat("Anisotropic", &params.anisotropic, 0.0f, 1.0f);

            // Clearcoat
            ImGui::SliderFloat("Clearcoat", &params.clearcoat, 0.0f, 1.0f);

            // Clearcoat gloss
            ImGui::SliderFloat("Clearcoat Gloss", &params.clearcoatGloss, 0.0f, 1.0f);

            // Index of Refraction (IOR)
            ImGui::SliderFloat("Index of Refraction (IOR)", &params.IOR, 1.0f, 2.5f);

            // Texture indices
            //ImGui::InputInt("Albedo Texture Index", &params.albedoTexIndex);
            //ImGui::InputInt("Roughness Texture Index", &params.roughnessTexIndex);
            //ImGui::InputInt("Metalness Texture Index", &params.metalnessTexIndex);
            //ImGui::InputInt("Normal Map Texture Index", &params.normalmapTexIndex);

            // Material type
            //ImGui::InputInt("Material Type", &params.materialType);
            int currentType             = static_cast<int>(params.materialType);
            if (ImGui::Combo("Material Type", &currentType, kMaterialTypesStr.data(), kMaterialTypesStr.size()))
            {
                params.materialType = currentType;
                std::cout << "selected: " << currentType << "\n";
            }

            // Emissive color
            if (ImGui::ColorEdit3("Emissive", glm::value_ptr(params.emissive), ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR))
            {
                enabledEmissive = params.emissive.length() > 0.;
            }

        }

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
