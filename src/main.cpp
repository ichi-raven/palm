#include "EC2S.hpp"

#include "../include/AppStates.hpp"

#include <iostream>

class Editor : public ec2s::State<palm::AppState, palm::CommonRegion>
{
    GEN_STATE(Editor, palm::AppState, palm::CommonRegion);

};

void Editor::init()
{
    std::cout << "init editor!\n";
}

void Editor::update()
{
    static int counter = 0;
    std::cout << "update editor!!!(count :  " << counter++ << ")\n";

    if (counter > 100)
    {
        exitApplication();
    }
}

Editor::~Editor()
{
    std::cout << "editor destructed!\n";
}

int main()
{

    ec2s::Application<palm::AppState, palm::CommonRegion> app;

    app.addState<Editor>(palm::AppState::eEditor);

    app.init(palm::AppState::eEditor);

    while (!app.endAll())
    {
        app.update();
    }

    std::cout << "exited!\n";

    return 0;
}
