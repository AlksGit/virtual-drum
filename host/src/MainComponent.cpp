#include "MainComponent.h"

#include "SerialDataSource.h"
#include "SerialPort.h"

namespace vdrum
{

MainComponent::MainComponent (const juce::String& commandLine)
{
    addAndMakeVisible (sceneView);
    setSize (1280, 720);
    setWantsKeyboardFocus (true);

    startSource (commandLine);
    startTimerHz (4);

    // Opening the audio device can take a second or two: do it once the window is shown.
    audioStatus = "Opening audio...";
    juce::Component::SafePointer<MainComponent> safeThis (this);
    juce::MessageManager::callAsync ([safeThis]
    {
        if (safeThis == nullptr)
            return;
        const auto error = safeThis->audio.start();
        safeThis->audioStatus = error.isEmpty() ? safeThis->audio.describeDevice() : "Audio off: " + error;
    });
}

MainComponent::~MainComponent()
{
    stopTimer();
    if (source != nullptr)
        source->stop(); // no listener call can happen after this returns
}

void MainComponent::startSource (const juce::String& commandLine)
{
    const auto arguments = juce::StringArray::fromTokens (commandLine, true);
    const auto viewIndex = arguments.indexOf ("--view");
    if (viewIndex >= 0 && viewIndex + 1 < arguments.size())
        sceneView.setCameraAzimuthDegrees (arguments[viewIndex + 1].getFloatValue());

    const auto fakeIndex = arguments.indexOf ("--fake");
    const auto portIndex = arguments.indexOf ("--port");

    if (portIndex >= 0 && portIndex + 1 < arguments.size())
    {
        source = std::make_unique<SerialDataSource> (arguments[portIndex + 1].unquoted().toStdString());
    }
    else if (fakeIndex < 0 && findReceiverPort().has_value())
    {
        source = std::make_unique<SerialDataSource>();
    }
    else
    {
        auto bpm = 96.0;
        if (fakeIndex >= 0 && fakeIndex + 1 < arguments.size() && arguments[fakeIndex + 1].getDoubleValue() > 0.0)
            bpm = arguments[fakeIndex + 1].getDoubleValue();

        auto fake = std::make_unique<FakeDataSource> (FakeDrummerSettings { bpm, 1 });
        fakeSource = fake.get();
        source = std::move (fake);
    }

    std::string error;
    if (source->start (*this, error))
        statusText = juce::String (source->getName());
    else
        statusText = "No data: " + juce::String (error);
}

void MainComponent::resized()
{
    sceneView.setBounds (getLocalBounds());
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (key.getTextCharacter() == 'm' || key.getTextCharacter() == 'M')
    {
        audio.setMuted (! audio.isMuted());
        return true;
    }

    if (fakeSource == nullptr)
        return false;

    if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
    {
        const auto step = key == juce::KeyPress::upKey ? 4.0 : -4.0;
        fakeSource->setTempo (juce::jlimit (40.0, 240.0, fakeSource->getTempo() + step));
        return true;
    }
    return false;
}

void MainComponent::frameReceived (const protocol::Frame& frame)
{
    if (const auto* hit = std::get_if<protocol::HitPacket> (&frame.packet))
        audio.pushHit ({ nearestPad (Quat::fromQ14 (hit->quat)), static_cast<float> (hit->velocity) / 127.0f });

    sceneState.apply (frame, steadySeconds());
}

void MainComponent::sourceFailed (const std::string& message)
{
    failureMessage = message;
    sourceStopped = true;
}

void MainComponent::timerCallback()
{
    if (sourceStopped.exchange (false))
        statusText = juce::String (source->getName()) + " stopped: " + juce::String (failureMessage);

    auto text = statusText + "   " + juce::String (juce::roundToInt (sceneView.getFramesPerSecond())) + " fps";
    if (fakeSource != nullptr)
        text << "   " << juce::String (juce::roundToInt (fakeSource->getTempo())) << " bpm (Up / Down)";
    text << "\n" << audioStatus << (audio.isMuted() ? "   [muted, M]" : "   (M to mute)");
    text << "\nDrag to orbit, wheel to zoom, double-click to reset";

    sceneView.setOverlayText (text);
}

} // namespace vdrum
