#pragma once

#include "DrumKit.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <memory>
#include <mutex>

namespace vdrum
{

// Sends hits as General MIDI drum notes to a MIDI output (a DAW, a virtual port...).
// sendHit() can be called from the data source thread; the output is chosen on the message
// thread. On macOS a virtual output named "Virtual Drum" can be created for DAWs to listen to;
// on Windows, use a virtual cable such as loopMIDI.
class MidiSender
{
public:
    static constexpr const char* kVirtualPortName = "Virtual Drum";

    struct Output
    {
        juce::String name;
        juce::String identifier; // empty for the virtual port
        bool isVirtual = false;
    };

    // Outputs that can be selected now (the virtual port first, where supported).
    static juce::Array<Output> availableOutputs();

    // Opens an output (message thread). Returns false and stays off if it cannot be opened.
    bool open (const Output& output);
    void close();

    // Name of the open output, empty when off.
    juce::String currentName() const;

    // Note on now, note off a few milliseconds later. Any thread; cheap when off.
    void sendHit (Pad pad, float velocity);

private:
    mutable std::mutex mutex;
    std::unique_ptr<juce::MidiOutput> output;
};

} // namespace vdrum
