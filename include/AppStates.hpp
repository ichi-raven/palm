/*****************************************************************//**
 * @file   AppStates.hpp
 * @brief  State keys and Common informations for this application
 * 
 * @author ichi-raven
 * @date   February 2024
 *********************************************************************/
#ifndef PALM_INCLUDE_APPSTATES_HPP_
#define PALM_INCLUDE_APPSTATES_HPP_

#include <vk2s/Device.hpp>
#include <EC2S.hpp>

namespace palm
{
    /**
     * @brief  Keys corresponding to the States that make up this application
     */
    enum class AppState
    {
        eEditor,
        eRenderer,
    };

    /**
     * @brief  Region shared between States
     */
    struct CommonRegion
    {
        CommonRegion()
            : device(vk2s::Device::Extensions{.useRayTracingExt = true, .useNVMotionBlurExt = false})
        {

        }

        //! vk2s device
        vk2s::Device device;
        //! vk2s window
        UniqueHandle<vk2s::Window> window;
        //! ec2s registry (representing scene)
        ec2s::Registry scene;
    };

}  // namespace palm

#endif