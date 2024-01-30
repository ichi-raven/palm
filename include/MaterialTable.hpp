#ifndef VKPT_INCLUDE_MATERIALTABLE_HPP_
#define VKPT_INCLUDE_MATERIALTABLE_HPP_

#include <cstdint>

#include <glm/glm.hpp>

#include <vk2s/Device.hpp>
#include <vk2s/AssetLoader.hpp>

namespace vkpt
{
    enum class MaterialType : int32_t
    {
        eLambert    = 0,
        eConductor  = 1,
        eDielectric = 2,
        eMaterialNum
    };

    struct MaterialData  // std430
    {
        glm::vec4 albedo;
        glm::vec4 emissive;
        int32_t texIndex;
        MaterialType materialType;
        float alpha;
        float IOR;
    };

    struct Material
    {
        MaterialData data;
        std::string_view name;
        Handle<vk2s::Image> texture;

        void convertFrom(vk2s::Device& device, const vk2s::AssetLoader::Material& hostMat)
        {
            data.materialType = vkpt::MaterialType::eLambert;  // default
            if (hostMat.emissive && glm::length(*hostMat.emissive) > 1.0)
            {
                data.emissive = *hostMat.emissive;
            }
            else
            {
                data.emissive = glm::vec4(0.);
            }

            if (std::holds_alternative<glm::vec4>(hostMat.diffuse))
            {
                data.albedo   = std::get<glm::vec4>(hostMat.diffuse);
                data.texIndex = -1;
            }
            else
            {
                data.albedo = glm::vec4(0.3f, 0.3f, 0.3f, 1.f);  // DEBUG COLOR

                const auto& hostTexture = std::get<vk2s::AssetLoader::Texture>(hostMat.diffuse);
                const auto width        = hostTexture.width;
                const auto height       = hostTexture.height;
                const auto size         = width * height * static_cast<uint32_t>(STBI_rgb_alpha);

                vk::ImageCreateInfo ci;
                ci.arrayLayers   = 1;
                ci.extent        = vk::Extent3D(width, height, 1);
                ci.format        = vk::Format::eR8G8B8A8Srgb;
                ci.imageType     = vk::ImageType::e2D;
                ci.mipLevels     = 1;
                ci.usage         = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
                ci.initialLayout = vk::ImageLayout::eUndefined;

                texture = device.create<vk2s::Image>(ci, vk::MemoryPropertyFlagBits::eDeviceLocal, size, vk::ImageAspectFlagBits::eColor);

                texture->write(hostTexture.pData, size);
            }

            if (hostMat.specular && hostMat.shininess && glm::length(*hostMat.specular) > 1.0)
            {
                data.materialType = vkpt::MaterialType::eConductor;
                data.albedo       = *hostMat.specular;
                data.alpha        = 1. - *hostMat.shininess / 1000.;
            }

            if (hostMat.IOR && *hostMat.IOR > 1.0)
            {
                data.materialType = vkpt::MaterialType::eDielectric;
                data.albedo       = glm::vec4(1.0);
                data.IOR          = *hostMat.IOR;
            }
        }
    };

    class MaterialTable
    {
    public:
        MaterialTable();
        MaterialTable(const std::vector<Material>& materials);

        void add(const vk::ArrayProxy<const Material> materials);

        void clear();

        size_t size() const;

        size_t find(std::string_view name) const;

        MaterialData& get(const size_t index);

        const MaterialData& get(const size_t index) const;

        void build(vk2s::Device& device);

        Handle<vk2s::Buffer> getBuffer() const;

        Handle<vk2s::Sampler> getSampler() const;

        const std::vector<Handle<vk2s::Image>>& getTextures() const;

    private:
        std::vector<MaterialData> mData;
        std::vector<Handle<vk2s::Image>> mTextures;
        std::unordered_map<std::string, size_t> mMap;  // name to index

        Handle<vk2s::Buffer> mBuffer;
        Handle<vk2s::Sampler> mSampler;
    };
}  // namespace vkpt
#endif