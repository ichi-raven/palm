/*****************************************************************//**
 * @file   Integrator.hpp
 * @brief  
 * 
 * @author ichi-raven
 * @date   October 2024
 *********************************************************************/
#ifndef PALM_INCLUDE_INTEGRATOR_HPP_
#define PALM_INCLUDE_INTEGRATOR_HPP_

#include <vk2s/Device.hpp>
#include <vk2s/Camera.hpp>

#include <EC2S.hpp>

#include "Film.hpp"

namespace palm
{
    class Integrator
    {
    public:
        Integrator(vk2s::Device& device, ec2s::Registry& scene, Handle<vk2s::Image> outputImage)
            : mDevice(device)
            , mScene(scene)
            , mOutputImage(outputImage)
            {}
         
        virtual ~Integrator(){}

        virtual void showConfigImGui() = 0;

        virtual void updateShaderResources() = 0;

        virtual void sample(Handle<vk2s::Fence> fence, Handle<vk2s::Command> command) = 0;

    protected:

        vk2s::Device& mDevice;
        ec2s::Registry& mScene;
        Handle<vk2s::Image> mOutputImage;
    };
}  // namespace palm

#endif