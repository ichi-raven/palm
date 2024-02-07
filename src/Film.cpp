//#include "../include/Film.hpp"
//
//#include <stb_image_write.h>
//
//#include <iostream>
//
//namespace vkpt
//{
//    Film::Film(const uint32_t width, const uint32_t height, const vk::Format format)
//        : mWidth(width)
//        , mHeight(height)
//        , mFormat(format)
//    {
//    }
//
//    Film::Film(vk2s::Device& device, const uint32_t width, const uint32_t height, const vk::Format format)
//        : mWidth(width)
//        , mHeight(height)
//        , mFormat(format)
//    {
//        build(device);
//    }
//
//    void Film::build(vk2s::Device& device)
//    {
//        {
//            const uint32_t size = mWidth * mHeight * vk2s::Compiler::getSizeOfFormat(mFormat);
//
//            vk::ImageCreateInfo ci;
//            ci.arrayLayers   = 1;
//            ci.extent        = vk::Extent3D(mWidth, mHeight, 1);
//            ci.format        = mFormat;
//            ci.imageType     = vk::ImageType::e2D;
//            ci.mipLevels     = 1;
//            ci.usage         = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eStorage;
//            ci.initialLayout = vk::ImageLayout::eUndefined;
//
//            // summation format
//            mImage = device.create<vk2s::Image>(ci, vk::MemoryPropertyFlagBits::eDeviceLocal, size, vk::ImageAspectFlagBits::eColor);
//        }
//
//        // staging image
//        {
//            constexpr vk::Format outputFormat = vk::Format::eR8G8B8A8Unorm;
//            const uint32_t channelSize        = vk2s::Compiler::getSizeOfFormat(outputFormat);
//            const uint32_t size               = mWidth * mHeight * channelSize;
//            mStagingBuffer                    = device.create<vk2s::Buffer>(vk::BufferCreateInfo({}, size, vk::BufferUsageFlagBits::eTransferDst), vk::MemoryPropertyFlagBits::eHostVisible);
//        }
//
//        UniqueHandle<vk2s::Command> cmd = device.create<vk2s::Command>();
//        cmd->begin(true);
//        cmd->transitionImageLayout(mImage.get(), vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral);
//        cmd->end();
//        cmd->execute();
//    }
//
//    void Film::write(vk2s::Device& device, std::string_view path)
//    {
//        constexpr vk::Format outputFormat = vk::Format::eR8G8B8A8Unorm;
//        const uint32_t channelSize        = vk2s::Compiler::getSizeOfFormat(outputFormat);
//        const uint32_t size               = mWidth * mHeight * channelSize;
//
//        const auto region = vk::ImageCopy()
//                                .setExtent({ mWidth, mHeight, 1 })
//                                .setSrcSubresource({ vk::ImageAspectFlagBits::eColor, 0, 0, 1 })
//                                .setSrcOffset({ 0, 0, 0 })
//                                .setDstSubresource({ vk::ImageAspectFlagBits::eColor, 0, 0, 1 })
//                                .setDstOffset({ 0, 0, 0 });
//
//        UniqueHandle<vk2s::Command> cmd = device.create<vk2s::Command>();
//        cmd->begin(true);
//        cmd->transitionImageLayout(mImage.get(), vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferSrcOptimal);
//        cmd->copyImageToBuffer(mImage.get(), mStagingBuffer.get(), mWidth, mHeight);
//        cmd->transitionImageLayout(mImage.get(), vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eGeneral);
//        cmd->end();
//        cmd->execute();
//
//        device.getVkDevice()->waitIdle();
//
//        std::vector<uint8_t> output(mWidth * mHeight * 3);
//        {
//            std::uint8_t* p = reinterpret_cast<std::uint8_t*>(device.getVkDevice()->mapMemory(mStagingBuffer->getVkDeviceMemory().get(), 0, size));
//            for (int h = 0; h < mHeight; h++)
//            {
//                for (int w = 0; w < mWidth; w++)
//                {
//                    int index             = h * mWidth + w;
//                    output[index * 3 + 0] = p[index * 4 + 0];
//                    output[index * 3 + 1] = p[index * 4 + 1];
//                    output[index * 3 + 2] = p[index * 4 + 2];
//                }
//            }
//
//            device.getVkDevice()->unmapMemory(mStagingBuffer->getVkDeviceMemory().get());
//        }
//
//        int res = stbi_write_png(path.data(), mWidth, mHeight, 3, output.data(), mWidth * 3);
//        if (res == 0)
//        {
//            std::cerr << "failed to output!\n";
//        }
//    }
//
//    Handle<vk2s::Image> Film::getImage()
//    {
//        return mImage;
//    }
//
//    std::pair<uint32_t, uint32_t> Film::getBounds() const
//    {
//        return { mWidth, mHeight };
//    }
//
//    vk::Format Film::getFormat() const
//    {
//        return mFormat;
//    }
//
//}  // namespace vkpt
