#pragma once

#include "AudioEngine.h"
#include "DataSource.h"
#include "FakeDataSource.h"
#include "KitLibrary.h"
#include "MidiSender.h"
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
// Command line: --fake [bpm] forces the simulated drummer, --port <path> a serial port,
// --view <degrees> sets the initial camera azimuth (0 = front of the kit, 180 = behind).
// --no-audio disables the sound output.
// Without options the receiver is used when detected, the simulated drummer otherwise.
// Keys: Up / Down change the tempo of the simulated drummer, M mutes the sound, K selects the
// next sound kit (also in the kit list, top right). The MIDI list under it sends every hit to a
// MIDI output as General MIDI drum notes (for a DAW).
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

    // Sound kits.
    void rebuildKitMenu();
    void selectKit (int index);
    void kitMenuChanged();

    // MIDI output.
    void rebuildMidiMenu();
    void midiMenuChanged();
    void openMidiOutput (const juce::String& name);

    // Audio driver type (shared, low latency, exclusive, ASIO...).
    void rebuildAudioMenu();
    void audioMenuChanged();

    SceneState sceneState;
    SceneView sceneView { sceneState };
    KitLibrary kitLibrary;                  // before `audio`, whose kit factory uses it
    AudioEngine audio;
    juce::ComboBox kitSelector;
    MidiSender midiSender;
    juce::ComboBox midiSelector;
    juce::Array<MidiSender::Output> midiOutputs; // as listed in midiSelector
    juce::ComboBox audioSelector;
    juce::StringArray audioDriverTypes;           // as listed in audioSelector
    int currentKit = 0;
    std::unique_ptr<juce::PropertiesFile> settings;
    juce::String audioStatus;
    std::unique_ptr<DataSource> source;
    FakeDataSource* fakeSource = nullptr; // non-null when `source` is the simulated drummer

    std::atomic<bool> sourceStopped { false };
    std::string failureMessage;           // written before sourceStopped is set
    juce::String statusText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace vdrum
