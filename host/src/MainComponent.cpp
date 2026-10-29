#include "MainComponent.h"

#include "SerialDataSource.h"
#include "SerialPort.h"

namespace vdrum
{

namespace
{

constexpr int kRefreshKitsId = 1000;
constexpr int kOpenKitsFolderId = 1001;
constexpr int kMidiOffId = 1;
constexpr int kMidiRefreshId = 900;
constexpr int kMidiFirstOutputId = 2;
const char* const kKitSetting = "kit";
const char* const kMidiSetting = "midiOutput";

void styleSelector (juce::ComboBox& box)
{
    box.setWantsKeyboardFocus (false); // keep the keyboard shortcuts working
    box.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xd010141c));
    box.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff27324a));
    box.setColour (juce::ComboBox::textColourId, juce::Colour (0xffd8e0ee));
    box.setColour (juce::ComboBox::arrowColourId, juce::Colour (0xff36c8ff));
}

std::unique_ptr<juce::PropertiesFile> openSettings()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "Virtual Drum";
    options.folderName = "Virtual Drum";
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    return std::make_unique<juce::PropertiesFile> (options);
}

} // namespace

MainComponent::MainComponent (const juce::String& commandLine)
    : settings (openSettings())
{
    addAndMakeVisible (sceneView);

    // The kit list sits on top of the GL view: it has to be a child of that component.
    kitSelector.setTooltip ("Sound kit (K for the next one)");
    styleSelector (kitSelector);
    kitSelector.onChange = [this] { kitMenuChanged(); };
    sceneView.addAndMakeVisible (kitSelector);

    rebuildKitMenu();
    selectKit (kitLibrary.indexOf (settings->getValue (kKitSetting, synthKitName (SynthKit::studio))));

    midiSelector.setTooltip ("Send every hit to a MIDI output (General MIDI drums, channel 10)");
    styleSelector (midiSelector);
    midiSelector.onChange = [this] { midiMenuChanged(); };
    sceneView.addAndMakeVisible (midiSelector);
    rebuildMidiMenu();
    openMidiOutput (settings->getValue (kMidiSetting));

    setSize (1280, 720);
    setWantsKeyboardFocus (true);

    startSource (commandLine);
    startTimerHz (4);

    if (commandLine.contains ("--no-audio"))
    {
        audioStatus = "Audio off (--no-audio)";
        return;
    }

    // Opening the audio device blocks the message thread for ~1.5 s (driver types are tried in
    // turn), and JUCE needs that thread to paint the overlay of each GL frame. Open it once the
    // first frames are on screen: the scene then pauses briefly instead of starting black.
    // (WASAPI needs COM initialised on the calling thread, so it stays on the message thread.)
    audioStatus = "Opening audio...";
    juce::Component::SafePointer<MainComponent> safeThis (this);
    juce::Timer::callAfterDelay (1500, [safeThis]
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
    midiSender.close();
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
    kitSelector.setBounds (sceneView.getWidth() - 12 - 240, 12, 240, 28);
    midiSelector.setBounds (kitSelector.getBounds().translated (0, 34));
}

void MainComponent::rebuildMidiMenu()
{
    midiOutputs = MidiSender::availableOutputs();
    midiSelector.clear (juce::dontSendNotification);
    midiSelector.addItem ("MIDI out: off", kMidiOffId);
    for (int i = 0; i < midiOutputs.size(); ++i)
        midiSelector.addItem ("MIDI out: " + midiOutputs[i].name + (midiOutputs[i].isVirtual ? " (virtual port)" : ""),
                              kMidiFirstOutputId + i);
    midiSelector.addSeparator();
    midiSelector.addItem ("Refresh MIDI outputs", kMidiRefreshId);

    auto selected = kMidiOffId;
    for (int i = 0; i < midiOutputs.size(); ++i)
        if (midiOutputs[i].name == midiSender.currentName())
            selected = kMidiFirstOutputId + i;
    midiSelector.setSelectedId (selected, juce::dontSendNotification);
}

void MainComponent::openMidiOutput (const juce::String& name)
{
    midiSender.close();
    for (const auto& output : midiOutputs)
        if (name.isNotEmpty() && output.name == name)
            midiSender.open (output);

    settings->setValue (kMidiSetting, midiSender.currentName());
    settings->saveIfNeeded();
    rebuildMidiMenu();
}

void MainComponent::midiMenuChanged()
{
    const auto id = midiSelector.getSelectedId();
    if (id == kMidiRefreshId)
    {
        const auto current = midiSender.currentName();
        rebuildMidiMenu();
        openMidiOutput (current);
        return;
    }

    const auto index = id - kMidiFirstOutputId;
    openMidiOutput (index >= 0 && index < midiOutputs.size() ? midiOutputs[index].name : juce::String());
}

void MainComponent::rebuildKitMenu()
{
    kitSelector.clear (juce::dontSendNotification);
    const auto& kits = kitLibrary.kits();
    for (std::size_t i = 0; i < kits.size(); ++i)
    {
        if (i > 0 && ! kits[i].synthesised && kits[i - 1].synthesised)
            kitSelector.addSeparator();
        kitSelector.addItem ((kits[i].synthesised ? "" : "Samples: ") + kits[i].name, static_cast<int> (i) + 1);
    }
    kitSelector.addSeparator();
    kitSelector.addItem ("Refresh list", kRefreshKitsId);
    kitSelector.addItem ("Open kits folder...", kOpenKitsFolderId);
    kitSelector.setSelectedId (currentKit + 1, juce::dontSendNotification);
}

void MainComponent::selectKit (int index)
{
    const auto& kits = kitLibrary.kits();
    if (kits.empty())
        return;

    currentKit = juce::jlimit (0, static_cast<int> (kits.size()) - 1, index);
    kitSelector.setSelectedId (currentKit + 1, juce::dontSendNotification);

    const auto entry = kits[static_cast<std::size_t> (currentKit)];
    audio.setKit ([this, entry] (double sampleRate) { return kitLibrary.load (entry, sampleRate); });

    settings->setValue (kKitSetting, entry.name);
    settings->saveIfNeeded();
}

void MainComponent::kitMenuChanged()
{
    const auto id = kitSelector.getSelectedId();

    if (id == kRefreshKitsId || id == kOpenKitsFolderId)
    {
        const auto currentName = kitLibrary.kits()[static_cast<std::size_t> (currentKit)].name;
        if (id == kOpenKitsFolderId)
            kitLibrary.createUserKitsFolder().startAsProcess(); // opens it in Explorer / Finder

        kitLibrary.rescan();
        currentKit = kitLibrary.indexOf (currentName);
        rebuildKitMenu();
        return;
    }

    if (id > 0)
        selectKit (id - 1);
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (key.getTextCharacter() == 'm' || key.getTextCharacter() == 'M')
    {
        audio.setMuted (! audio.isMuted());
        return true;
    }

    if (key.getTextCharacter() == 'k' || key.getTextCharacter() == 'K')
    {
        selectKit ((currentKit + 1) % static_cast<int> (kitLibrary.kits().size()));
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
    {
        const auto pad = nearestPad (Quat::fromQ14 (hit->quat));
        const auto velocity = static_cast<float> (hit->velocity) / 127.0f;
        audio.pushHit ({ pad, velocity });
        midiSender.sendHit (pad, velocity);
    }

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
    text << "\nKit: " << kitLibrary.kits()[static_cast<std::size_t> (currentKit)].name << "   (K for the next one)";
    const auto midiName = midiSender.currentName();
    text << "   MIDI out: " << (midiName.isEmpty() ? juce::String ("off") : midiName);
    text << "\nDrag to orbit, wheel to zoom, double-click to reset";

    sceneView.setOverlayText (text);
}

} // namespace vdrum
