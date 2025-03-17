/*****************************************************************/ /**
 * @file   Integrator.cpp
 * @brief  source file of Integrator class (abstruct)
 * 
 * @author ichi-raven
 * @date   March 2025
 *********************************************************************/

#include "../include/Integrators/Integrator.hpp"

#include "omp.h"

#include <numbers>

namespace palm
{
    Integrator::Integrator(vk2s::Device& device, ec2s::Registry& scene, Handle<vk2s::Image> outputImage)
        : mDevice(device)
        , mScene(scene)
        , mOutputImage(outputImage)
    {
        // create dummy image
        {
            // dummy texture
// dummy texture
#ifndef NDEBUG
            constexpr uint8_t kDummyColor[] = { 255, 0, 255, 0 };  // Magenta
#else
            constexpr uint8_t kDummyColor[] = { 0, 0, 0, 0 };  // Black
#endif
            const auto format   = vk::Format::eR8G8B8A8Srgb;
            const uint32_t size = vk2s::Compiler::getSizeOfFormat(format);  // 1 * 1

            vk::ImageCreateInfo ci;
            ci.arrayLayers   = 1;
            ci.extent        = vk::Extent3D(1, 1, 1);  // 1 * 1
            ci.format        = format;
            ci.imageType     = vk::ImageType::e2D;
            ci.mipLevels     = 1;
            ci.usage         = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
            ci.initialLayout = vk::ImageLayout::eUndefined;

            // change format to pooling
            mDummyTexture = device.create<vk2s::Image>(ci, vk::MemoryPropertyFlagBits::eDeviceLocal, size, vk::ImageAspectFlagBits::eColor);
            mDummyTexture->write(kDummyColor, size);

            UniqueHandle<vk2s::Command> cmd = device.create<vk2s::Command>();
            cmd->begin(true);
            cmd->transitionImageLayout(mDummyTexture.get(), vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal);
            cmd->end();
            cmd->execute();
        }
    }

    Integrator::~Integrator()
    {
        mDevice.destroy(mDummyTexture);
    }

    template <typename T>
    inline T toGray(T r, T g, T b)
    {
        // ITU-R
        return 0.299 * r + 0.587 * g + 0.114 * b;
    }

    Handle<vk2s::Image> Integrator::buildPDFImage(Handle<vk2s::Image> image)
    {
        const auto extent = image->getVkExtent();
        const auto format = image->getVkFormat();

        if (format != vk::Format::eR8G8B8A8Unorm)
        {
            throw std::runtime_error("invalid texture format for building PDF!");
        }

        const uint32_t channelSize = vk2s::Compiler::getSizeOfFormat(format);
        const uint32_t size        = extent.width * extent.height * channelSize;

        const auto region =
            vk::ImageCopy().setExtent(extent).setSrcSubresource({ vk::ImageAspectFlagBits::eColor, 0, 0, 1 }).setSrcOffset({ 0, 0, 0 }).setDstSubresource({ vk::ImageAspectFlagBits::eColor, 0, 0, 1 }).setDstOffset({ 0, 0, 0 });

        // create staging buffer
        UniqueHandle<vk2s::Buffer> stagingBuffer = mDevice.create<vk2s::Buffer>(vk::BufferCreateInfo({}, size, vk::BufferUsageFlagBits::eTransferDst), vk::MemoryPropertyFlagBits::eHostVisible);

        UniqueHandle<vk2s::Fence> fence = mDevice.create<vk2s::Fence>();
        fence->reset();
        UniqueHandle<vk2s::Command> cmd = mDevice.create<vk2s::Command>();
        cmd->begin(true);
        cmd->transitionImageLayout(image.get(), vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferSrcOptimal);
        cmd->copyImageToBuffer(image.get(), stagingBuffer.get(), extent.width, extent.height);
        //cmd->transitionImageLayout(outputImage.get(), vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eGeneral);
        cmd->end();
        cmd->execute(fence);

        fence->wait();

        omp_lock_t omp_lock;
        omp_init_lock(&omp_lock);

        const std::uint8_t* p = reinterpret_cast<std::uint8_t*>(mDevice.getVkDevice()->mapMemory(stagingBuffer->getVkDeviceMemory().get(), 0, size));

        // step 1 : accumulate all pixel value
        double sum = 0.;

        {
            omp_set_num_threads(omp_get_max_threads());
#pragma omp parallel for
            for (size_t h = 0; h < extent.height; ++h)  // int for OpenMP
            {
                double partialSum = 0;

                for (size_t w = 0; w < extent.width; ++w)
                {
                    const size_t index = h * extent.width + w;
                    partialSum += toGray<double>(p[index * 4 + 0], p[index * 4 + 1], p[index * 4 + 2]);
                }

                // exclusive
                {
                    omp_set_lock(&omp_lock);
                    sum += partialSum;
                    omp_unset_lock(&omp_lock);
                }
            }
        }
        omp_destroy_lock(&omp_lock);

        // step 2: calculate pdf from sum and each pixel value
        std::vector<float> pdf(extent.width * extent.height, 0);
        {
            omp_set_num_threads(omp_get_max_threads());
#pragma omp parallel for
            for (size_t h = 0; h < extent.height; ++h)  // int for OpenMP
            {
                for (size_t w = 0; w < extent.width; ++w)
                {
                    const size_t index = h * extent.width + w;
                    const double v     = 1. * w / extent.width;
                    const double coef  = sin(std::numbers::pi * v);
                    pdf[index]         = coef * toGray<double>(p[index * 4 + 0], p[index * 4 + 1], p[index * 4 + 2]) / sum;
                }
            }
        }

        mDevice.getVkDevice()->unmapMemory(stagingBuffer->getVkDeviceMemory().get());

        // write pdf data to image
        Handle<vk2s::Image> pdfImage;
        {
            const auto pdfFormat = vk::Format::eR32Sfloat;
            const uint32_t size  = extent.width * extent.height * vk2s::Compiler::getSizeOfFormat(pdfFormat);

            vk::ImageCreateInfo ci;
            ci.arrayLayers   = 1;
            ci.extent        = vk::Extent3D(extent.width, extent.height, 1);
            ci.format        = pdfFormat;
            ci.imageType     = vk::ImageType::e2D;
            ci.mipLevels     = 1;
            ci.usage         = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eStorage;
            ci.initialLayout = vk::ImageLayout::eUndefined;

            pdfImage = mDevice.create<vk2s::Image>(ci, vk::MemoryPropertyFlagBits::eDeviceLocal, size, vk::ImageAspectFlagBits::eColor);

            pdfImage->write(pdf.data(), pdf.size() * vk2s::Compiler::getSizeOfFormat(pdfFormat));

            fence->reset();
            cmd->begin(true);
            cmd->transitionImageLayout(image.get(), vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eGeneral);
            cmd->transitionImageLayout(pdfImage.get(), vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral);
            cmd->end();
            cmd->execute(fence);

            fence->wait();
        }

        return pdfImage;
    }
}  // namespace palm
