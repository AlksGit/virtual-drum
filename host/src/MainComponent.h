#pragma once

#include "SceneView.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace vdrum
{

// Top-level content of the main window. For now it only hosts the 3D scene;
// data sources, controls and audio will be added by later tasks.
class MainComponent final : public juce::Component
{
public:
    MainComponent();

    void resized() override;

private:
    SceneView sceneView;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace vdrum
