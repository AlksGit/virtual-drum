#pragma once

#include "DataSource.h"
#include "FakeDataSource.h"
#include "SceneState.h"
#include "SceneView.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <memory>

namespace vdrum
{

// Top-level content of the main window: picks a data source, feeds the scene state and
// hosts the 3D view.
//
// Command line: --fake [bpm] forces the simulated drummer, --port <path> a serial port.
// Without options the receiver is used when detected, the simulated drummer otherwise.
// Keys: Up / Down change the tempo of the simulated drummer.
class MainComponent final : public juce::Component,
                            private DataSource::Listener,
                            private juce::Timer
{
public:
    explicit MainComponent (const juce::String& commandLine = {});
    ~MainComponent() override;

    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

private:
    // DataSource::Listener, called on the source thread.
    void frameReceived (const protocol::Frame& frame) override;
    void sourceFailed (const std::string& message) override;

    void timerCallback() override;
    void startSource (const juce::String& commandLine);

    SceneState sceneState;
    SceneView sceneView { sceneState };
    std::unique_ptr<DataSource> source;
    FakeDataSource* fakeSource = nullptr; // non-null when `source` is the simulated drummer

    std::atomic<bool> sourceStopped { false };
    std::string failureMessage;           // written before sourceStopped is set
    juce::String statusText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace vdrum
