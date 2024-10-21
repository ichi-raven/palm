#include "EC2S.hpp"

#include "../include/AppStates.hpp"
#include "../include/States/Editor.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

int main()
{

    ec2s::Application<palm::AppState, palm::CommonRegion> app;

    app.mpCommonRegion->window = app.mpCommonRegion->device.create<vk2s::Window>(1920, 1080, 3, "palm window", false);

    app.addState<palm::Editor>(palm::AppState::eEditor);

    app.init(palm::AppState::eEditor);

    while (!app.endAll())
    {
        app.update();
    }

    return 0;
}
