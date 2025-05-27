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
#include <omp.h>

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

            //! Index of the Entity's mesh with this Emitter (only for area emitter)
            int32_t faceNum        = 0;
            int32_t meshIndex = -1;
            int32_t primitiveIndex = -1;
            int32_t padding     = 0;

            //! The luminous component of this Emitter
            glm::vec3 emissive = glm::vec3(0.);
            //! Index to emissiveTex
            int32_t texIndex = -1;
        };

         template <typename T>
        inline T toGray(T r, T g, T b)
        {
            // ITU-R
            return 0.299 * r + 0.587 * g + 0.114 * b;
        }

        void buildPDFImage(vk2s::Device& device)
        {
            const auto extent = emissiveTex->getVkExtent();
            const auto format = emissiveTex->getVkFormat();

            if (format != vk::Format::eR8G8B8A8Unorm)
            {
                throw std::runtime_error("invalid texture format for building PDF!");
            }

            const uint32_t channelSize = vk2s::Compiler::getSizeOfFormat(format);
            const uint32_t size        = extent.width * extent.height * channelSize;

            const auto region =
                vk::ImageCopy().setExtent(extent).setSrcSubresource({ vk::ImageAspectFlagBits::eColor, 0, 0, 1 }).setSrcOffset({ 0, 0, 0 }).setDstSubresource({ vk::ImageAspectFlagBits::eColor, 0, 0, 1 }).setDstOffset({ 0, 0, 0 });

            // create staging buffer
            UniqueHandle<vk2s::Buffer> stagingBuffer = device.create<vk2s::Buffer>(vk::BufferCreateInfo({}, size, vk::BufferUsageFlagBits::eTransferDst), vk::MemoryPropertyFlagBits::eHostVisible);

            UniqueHandle<vk2s::Fence> fence = device.create<vk2s::Fence>();
            fence->reset();
            UniqueHandle<vk2s::Command> cmd = device.create<vk2s::Command>();
            cmd->begin(true);
            cmd->transitionImageLayout(emissiveTex.get(), vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferSrcOptimal);
            cmd->copyImageToBuffer(emissiveTex.get(), stagingBuffer.get(), extent.width, extent.height);
            cmd->end();
            cmd->execute(fence);

            fence->wait();

            const std::uint8_t* p = reinterpret_cast<std::uint8_t*>(device.getVkDevice()->mapMemory(stagingBuffer->getVkDeviceMemory().get(), 0, size));

            // step 1 : accumulate all pixel value
            std::vector<double> sums(extent.height, 0);
            {
                omp_set_num_threads(omp_get_max_threads());
#pragma omp parallel for
                for (int h = 0; h < extent.height; ++h)  // int for OpenMP
                {
                    for (size_t w = 0; w < extent.width; ++w)
                    {
                        const size_t index = h * extent.width + w;
                        sums[h] += toGray<double>(p[index * 4 + 0], p[index * 4 + 1], p[index * 4 + 2]);
                    }
                }
            }

            // step 2: calculate pdf from sum and each pixel value
            std::vector<float> pdf(extent.width * extent.height, 0);
            std::vector<float> cdf(extent.width * extent.height, 0);
            {
                omp_set_num_threads(omp_get_max_threads());
#pragma omp parallel for
                for (int h = 0; h < extent.height; ++h)  // int for OpenMP
                {
                    for (size_t w = 0; w < extent.width; ++w)
                    {
                        const size_t index = h * extent.width + w;
                        pdf[index]         = toGray<double>(p[index * 4 + 0], p[index * 4 + 1], p[index * 4 + 2]) / sums[h];
                    }
                }
            }

            device.getVkDevice()->unmapMemory(stagingBuffer->getVkDeviceMemory().get());

            // write pdf and cdf data to image
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

                pdfTex = device.create<vk2s::Image>(ci, vk::MemoryPropertyFlagBits::eDeviceLocal, size, vk::ImageAspectFlagBits::eColor);
                cdfTex = device.create<vk2s::Image>(ci, vk::MemoryPropertyFlagBits::eDeviceLocal, size, vk::ImageAspectFlagBits::eColor);

                pdfTex->write(pdf.data(), pdf.size() * vk2s::Compiler::getSizeOfFormat(pdfFormat));

                fence->reset();
                cmd->begin(true);
                cmd->transitionImageLayout(emissiveTex.get(), vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eGeneral);
                cmd->transitionImageLayout(pdfTex.get(), vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral);
                cmd->transitionImageLayout(cdfTex.get(), vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral);
                cmd->end();
                cmd->execute(fence);

                fence->wait();
            }
        }

        //! GPU Parameters
        Params params;
        //! Texture representing the distribution of emissive values
        Handle<vk2s::Image> emissiveTex;
        //! Texture representing the pdf of emissive values
        Handle<vk2s::Image> pdfTex;
        //! Texture representing the cdf of emissive values
        Handle<vk2s::Image> cdfTex;

        //! Entity that has this Emitter
        std::optional<ec2s::Entity> attachedEntity;
    };
}  // namespace palm

#endif
