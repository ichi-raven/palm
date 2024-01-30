#include "../include/MaterialTable.hpp"

namespace vkpt
{
    MaterialTable::MaterialTable()
    {
    }

    MaterialTable::MaterialTable(const std::vector<Material>& materials)
    {
        add(materials);
    }

    void MaterialTable::add(const vk::ArrayProxy<const Material> materials)
    {
        mData.reserve(mData.size() + materials.size());
        mTextures.reserve(mTextures.size() + materials.size());
        mMap.reserve(mMap.size() + materials.size());

        for (const auto& material : materials)
        {
            mMap.emplace(material.name, mData.size());
            mData.emplace_back(material.data);
            if (material.texture)
            {
                mTextures.emplace_back(material.texture);
            }
        }
    }

    void MaterialTable::clear()
    {
        mData.clear();
        mMap.clear();
        mTextures.clear();
    }

    size_t MaterialTable::size() const
    {
        return mData.size();
    }

    size_t MaterialTable::find(std::string_view name) const
    {
        assert(mMap.find(name.data()) != mMap.end() || !"invalid material name!");

        return mMap.find(name.data())->second;
    }

    MaterialData& MaterialTable::get(const size_t index)
    {
        assert(index >= 0 && index < mData.size() || !"invalid material index!");

        return mData[index];
    }

    const MaterialData& MaterialTable::get(const size_t index) const
    {
        assert(index >= 0 && index < mData.size() || !"invalid material index!");

        return mData[index];
    }

    void MaterialTable::build(vk2s::Device& device)
    {
        // build material buffer and textures
        const auto ubSize = sizeof(MaterialData) * mData.size();
        vk::BufferCreateInfo ci({}, ubSize, vk::BufferUsageFlagBits::eStorageBuffer);
        vk::MemoryPropertyFlags fb = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;

        mBuffer = device.create<vk2s::Buffer>(ci, fb);
        mBuffer->write(mData.data(), ubSize);

        vk::SamplerCreateInfo sci;
        mSampler = device.create<vk2s::Sampler>(sci);
    }

    Handle<vk2s::Buffer> MaterialTable::getBuffer() const
    {
        return mBuffer;
    }

    Handle<vk2s::Sampler> MaterialTable::getSampler() const
    {
        return mSampler;
    }

    const std::vector<Handle<vk2s::Image>>& MaterialTable::getTextures() const
    {
        return mTextures;
    }

}  // namespace vkpt
