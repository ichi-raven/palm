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

   
}  // namespace palm
