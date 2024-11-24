/*****************************************************************/ /**
 * @file   GraphicsPass.hpp
 * @brief  
 * 
 * @author ichi-raven
 * @date   October 2024
 *********************************************************************/
#ifndef PALM_INCLUDE_GRAPHICSPASS_HPP_
#define PALM_INCLUDE_GRAPHICSPASS_HPP_

#include <vk2s/Device.hpp>
#include <vector>

namespace palm
{
    /**
     * @brief  Struct representing the drawing path for rasterization
     */
    struct GraphicsPass
    {
        UniqueHandle<vk2s::RenderPass> renderpass;
        UniqueHandle<vk2s::Pipeline> pipeline;
        UniqueHandle<vk2s::Shader> vs;
        UniqueHandle<vk2s::Shader> fs;
        std::vector<Handle<vk2s::BindLayout>> bindLayouts;
    };
}  // namespace palm

#endif
