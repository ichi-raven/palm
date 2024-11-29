#include "EC2S.hpp"

#include "../include/AppStates.hpp"
#include "../include/States/Editor.hpp"
#include "../include/States/Renderer.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>

inline void setupImGuiStyle()
{
    ImGui::CreateContext();

#ifdef _WIN32  // HACK: load from Windows default Arial fonts
    {
        ImGuiIO& io = ImGui::GetIO();
        io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/Arial.ttf", 24.0f);
    }
#endif

    ImGuiStyle& style    = ImGui::GetStyle();
    style.Alpha          = 1.0;
    style.WindowRounding = 3;
    style.GrabRounding   = 1;
    style.GrabMinSize    = 20;
    style.FrameRounding  = 3;

    style.Colors[ImGuiCol_Text]                 = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_TextDisabled]         = ImVec4(0.00f, 0.40f, 0.41f, 1.00f);
    style.Colors[ImGuiCol_WindowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    style.Colors[ImGuiCol_Border]               = ImVec4(0.00f, 1.00f, 1.00f, 0.65f);
    style.Colors[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    style.Colors[ImGuiCol_FrameBg]              = ImVec4(0.44f, 0.80f, 0.80f, 0.18f);
    style.Colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.44f, 0.80f, 0.80f, 0.27f);
    style.Colors[ImGuiCol_FrameBgActive]        = ImVec4(0.44f, 0.81f, 0.86f, 0.66f);
    style.Colors[ImGuiCol_TitleBg]              = ImVec4(0.14f, 0.18f, 0.21f, 0.73f);
    style.Colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.00f, 0.00f, 0.00f, 0.54f);
    style.Colors[ImGuiCol_TitleBgActive]        = ImVec4(0.00f, 1.00f, 1.00f, 0.27f);
    style.Colors[ImGuiCol_MenuBarBg]            = ImVec4(0.00f, 0.00f, 0.00f, 0.20f);
    style.Colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.22f, 0.29f, 0.30f, 0.71f);
    style.Colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.00f, 1.00f, 1.00f, 0.44f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.00f, 1.00f, 1.00f, 0.74f);
    style.Colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_CheckMark]            = ImVec4(0.00f, 1.00f, 1.00f, 0.68f);
    style.Colors[ImGuiCol_SliderGrab]           = ImVec4(0.00f, 1.00f, 1.00f, 0.36f);
    style.Colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.00f, 1.00f, 1.00f, 0.76f);
    style.Colors[ImGuiCol_Button]               = ImVec4(0.00f, 0.65f, 0.65f, 0.46f);
    style.Colors[ImGuiCol_ButtonHovered]        = ImVec4(0.01f, 1.00f, 1.00f, 0.43f);
    style.Colors[ImGuiCol_ButtonActive]         = ImVec4(0.00f, 1.00f, 1.00f, 0.62f);
    style.Colors[ImGuiCol_Header]               = ImVec4(0.00f, 1.00f, 1.00f, 0.33f);
    style.Colors[ImGuiCol_HeaderHovered]        = ImVec4(0.00f, 1.00f, 1.00f, 0.42f);
    style.Colors[ImGuiCol_HeaderActive]         = ImVec4(0.00f, 1.00f, 1.00f, 0.54f);
    style.Colors[ImGuiCol_ResizeGrip]           = ImVec4(0.00f, 1.00f, 1.00f, 0.54f);
    style.Colors[ImGuiCol_ResizeGripHovered]    = ImVec4(0.00f, 1.00f, 1.00f, 0.74f);
    style.Colors[ImGuiCol_ResizeGripActive]     = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_PlotLines]            = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_PlotLinesHovered]     = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_PlotHistogram]        = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_TextSelectedBg]       = ImVec4(0.00f, 1.00f, 1.00f, 0.22f);
}

int main()
{
    setupImGuiStyle();

    ec2s::Application<palm::AppState, palm::CommonRegion> app;

    app.mpCommonRegion->window = app.mpCommonRegion->device.create<vk2s::Window>(1920, 1080, 3, "palm window", false);

    app.addState<palm::Editor>(palm::AppState::eEditor);
    app.addState<palm::Renderer>(palm::AppState::eRenderer);

    app.init(palm::AppState::eEditor);

    while (!app.endAll())
    {
        app.update();
    }

    return 0;
}
