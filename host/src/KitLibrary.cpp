#include "KitLibrary.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace vdrum
{

namespace
{

constexpr double kMaxSampleSeconds = 8.0;

// File name stems accepted for each pad (lower case, without extension).
const std::array<std::vector<const char*>, kDrumKit.size()> kPadFileNames { {
    { "hihat", "hi-hat", "hh", "hat" },             // hiHat
    { "snare", "sd" },                              // snare
    { "hightom", "high-tom", "tom1", "rack1" },     // highTom
    { "midtom", "mid-tom", "tom2", "rack2" },       // midTom
    { "floortom", "floor-tom", "floor", "tom3" },   // floorTom
    { "crash", "cr" },                              // crash
    { "ride", "rd" },                               // ride
} };

// "Hi-Hat", "hi_hat" and "hihat" all name the same pad.
juce::String normaliseName (const juce::String& name)
{
    return name.toLowerCase().removeCharacters (" _-");
}

const char* const kReadme =
    "Virtual Drum - sample kits\n"
    "\n"
    "Create one folder per kit here. Inside, put one audio file per pad (WAV, AIFF or FLAC):\n"
    "  hihat  snare  hightom  midtom  floortom  crash  ride      (e.g. snare.wav)\n"
    "Missing pads use the built-in Studio sounds. Then choose \"Refresh list\" in the app.\n";

} // namespace

KitLibrary::KitLibrary()
{
    formats.registerBasicFormats();
    rescan();
}

juce::File KitLibrary::userKitsFolder()
{
    const auto overridePath = juce::SystemStats::getEnvironmentVariable ("VDRUM_KITS_DIR", {});
    if (overridePath.isNotEmpty())
        return juce::File (overridePath);

    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("Virtual Drum")
        .getChildFile ("Kits");
}

void KitLibrary::rescan()
{
    entries.clear();
    for (const auto synth : kSynthKits)
        entries.push_back ({ synthKitName (synth), true, synth, {} });

    const auto folder = userKitsFolder();
    if (! folder.isDirectory())
        return;

    auto folders = folder.findChildFiles (juce::File::findDirectories, false);
    std::sort (folders.begin(), folders.end(),
               [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; });

    for (const auto& kitFolder : folders)
        entries.push_back ({ kitFolder.getFileName(), false, SynthKit::studio, kitFolder });
}

int KitLibrary::indexOf (const juce::String& name) const
{
    for (std::size_t i = 0; i < entries.size(); ++i)
        if (entries[i].name == name)
            return static_cast<int> (i);
    return 0;
}

juce::File KitLibrary::createUserKitsFolder()
{
    const auto folder = userKitsFolder();
    if (folder.createDirectory().wasOk())
    {
        const auto readme = folder.getChildFile ("README.txt");
        if (! readme.existsAsFile())
            readme.replaceWithText (kReadme);
    }
    return folder;
}

std::vector<float> KitLibrary::loadFile (const juce::File& file, double sampleRate)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        return {};

    const auto length = static_cast<int> (std::min<juce::int64> (reader->lengthInSamples,
                                                                 static_cast<juce::int64> (kMaxSampleSeconds * reader->sampleRate)));
    juce::AudioBuffer<float> buffer (static_cast<int> (reader->numChannels), length);
    if (! reader->read (&buffer, 0, length, 0, true, true))
        return {};

    // Mix to mono.
    std::vector<float> mono (static_cast<std::size_t> (length), 0.0f);
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto* data = buffer.getReadPointer (channel);
        for (int i = 0; i < length; ++i)
            mono[static_cast<std::size_t> (i)] += data[i] / static_cast<float> (buffer.getNumChannels());
    }

    // Resample to the device rate.
    if (std::abs (reader->sampleRate - sampleRate) > 0.5)
    {
        const auto ratio = reader->sampleRate / sampleRate;
        const auto outLength = static_cast<int> (std::floor (static_cast<double> (length) / ratio));
        std::vector<float> resampled (static_cast<std::size_t> (std::max (outLength, 0)), 0.0f);
        juce::LagrangeInterpolator interpolator;
        interpolator.process (ratio, mono.data(), resampled.data(), outLength);
        mono = std::move (resampled);
    }

    // Normalise like the built-in sounds, so that velocity behaves the same on every kit.
    float peak = 0.0f;
    for (const auto s : mono)
        peak = std::max (peak, std::abs (s));
    if (peak > 0.0f)
        for (auto& s : mono)
            s *= 0.9f / peak;

    return mono;
}

SampleBank KitLibrary::load (const KitEntry& kit, double sampleRate)
{
    if (kit.synthesised)
        return synthesiseKit (sampleRate, kit.synth);

    auto bank = synthesiseKit (sampleRate, SynthKit::studio); // fallback for missing pads
    const auto files = kit.folder.findChildFiles (juce::File::findFiles, false, "*.wav;*.aif;*.aiff;*.flac");

    for (std::size_t pad = 0; pad < kPadFileNames.size(); ++pad)
        for (const auto& file : files)
        {
            const auto stem = normaliseName (file.getFileNameWithoutExtension());
            const auto& names = kPadFileNames[pad];
            if (std::none_of (names.begin(), names.end(), [&stem] (const char* n) { return normaliseName (n) == stem; }))
                continue;

            auto sound = loadFile (file, sampleRate);
            if (! sound.empty())
            {
                bank[pad] = std::move (sound);
                break;
            }
        }

    return bank;
}

} // namespace vdrum
