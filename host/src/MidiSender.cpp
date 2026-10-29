#include "MidiSender.h"

#include "MidiMapping.h"

namespace vdrum
{

juce::Array<MidiSender::Output> MidiSender::availableOutputs()
{
    juce::Array<Output> outputs;
#if JUCE_MAC || JUCE_LINUX || JUCE_IOS
    outputs.add ({ kVirtualPortName, {}, true });
#endif
    for (const auto& device : juce::MidiOutput::getAvailableDevices())
        outputs.add ({ device.name, device.identifier, false });
    return outputs;
}

bool MidiSender::open (const Output& choice)
{
    auto opened = choice.isVirtual ? juce::MidiOutput::createNewDevice (choice.name)
                                   : juce::MidiOutput::openDevice (choice.identifier);
    if (opened == nullptr)
    {
        close();
        return false;
    }

    // The background thread sends the time-stamped note offs.
    opened->startBackgroundThread();

    const std::lock_guard<std::mutex> lock (mutex);
    output = std::move (opened);
    return true;
}

void MidiSender::close()
{
    std::unique_ptr<juce::MidiOutput> closing;
    {
        const std::lock_guard<std::mutex> lock (mutex);
        closing = std::move (output);
    }
    if (closing != nullptr)
        closing->stopBackgroundThread();
}

juce::String MidiSender::currentName() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    return output != nullptr ? output->getName() : juce::String();
}

void MidiSender::sendHit (Pad pad, float velocity)
{
    const std::lock_guard<std::mutex> lock (mutex);
    if (output == nullptr)
        return;

    const auto on = midi::noteOn (pad, velocity);
    const auto off = midi::noteOff (pad);
    output->sendMessageNow (juce::MidiMessage (on[0], on[1], on[2]));

    juce::MidiBuffer noteOffBuffer;
    noteOffBuffer.addEvent (juce::MidiMessage (off[0], off[1], off[2]), 0);
    output->sendBlockOfMessages (noteOffBuffer, juce::Time::getMillisecondCounterHiRes() + midi::kNoteLengthMs, 1000.0);
}

} // namespace vdrum
