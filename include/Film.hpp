#ifndef VKPT_INCLUDE_FILM_HPP_
#define VKPT_INCLUDE_FILM_HPP_

#include <vk2s/Device.hpp>

#include <cstdint>

namespace vkpt
{
    class Film
    {
    public:
        Film(const uint32_t width, const uint32_t height, const vk::Format format);

        Film(vk2s::Device& device, const uint32_t width, const uint32_t height, const vk::Format format);

        Film(const Film& film) = default;

        Film& operator=(const Film& film) = default;

        void build(vk2s::Device& device);

        Handle<vk2s::Image> getImage();

        std::pair<uint32_t, uint32_t> getBounds() const;

        vk::Format getFormat() const;

        void write(vk2s::Device& device, std::string_view path);

    private:
        const uint32_t mWidth;
        const uint32_t mHeight;
        const vk::Format mFormat;

        Handle<vk2s::Image> mImage;
        Handle<vk2s::Buffer> mStagingBuffer;

    };
}  // namespace vkpt

#endif
