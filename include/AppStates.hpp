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

namespace palm
{
    enum class AppState
    {
        eEditor,
        eRender,
    };

    struct CommonRegion
    {
        vk2s::Device device;
        UniqueHandle<vk2s::Window> window;
    };

}  // namespace palm

#endif