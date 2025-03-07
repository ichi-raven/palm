/*****************************************************************/ /**
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

namespace palm
{
    /**
     * @brief  All Integrator Interface
     */
    class Integrator
    {
    public:
        /** 
         * @brief  Constructor
         *  
         * @param device vk2s device
         * @param scene Scene to be rendered
         * @param outputImage Image to which the drawing result (current progress) for each frame is written
         */
        Integrator(vk2s::Device& device, ec2s::Registry& scene, Handle<vk2s::Image> outputImage);

        /** 
         * @brief  destructor (virtual)
         *  
         */
        virtual ~Integrator();

        /** 
         * @brief  Virtual function for setting parameters from GUI for each integrator
         * @detail Called between ImGui::Begin() and ImGui::End()
         *  
         */
        virtual void showConfigImGui() = 0;

        /** 
         * @brief  Update shader resources
         * @detail Should not read or write to resources on the GPU other than here because Renderer synchronously controls the timing of calls
         *  
         */
        virtual void updateShaderResources() = 0;

        /** 
         * @brief  Luminance sampling per frame
         *  
         * @param command  Command buffer to write sampling instructions
         */
        virtual void sample(Handle<vk2s::Command> command) = 0;

        /**
         * @brief  Build a pdf image from the image according to the luminance value
         * 
         * @param image target image
         * @return pdf image
         */
        Handle<vk2s::Image> buildPDFImage(Handle<vk2s::Image> image);

    protected:
        //! Reference to vk2s device
        vk2s::Device& mDevice;
        //! Reference to scene
        ec2s::Registry& mScene;

        //! Handle of output destination image
        Handle<vk2s::Image> mOutputImage;

        //! Handle of dummy texture
        Handle<vk2s::Image> mDummyTexture;
    };
}  // namespace palm

#endif