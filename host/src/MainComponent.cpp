#include "MainComponent.h"

namespace vdrum
{

MainComponent::MainComponent()
{
    addAndMakeVisible (sceneView);
    setSize (1280, 720);
}

void MainComponent::resized()
{
    sceneView.setBounds (getLocalBounds());
}

} // namespace vdrum
