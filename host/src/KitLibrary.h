#pragma once

// The sound kits the user can choose from: the synthesised kits, plus one kit per folder of
// samples in the user kits folder (Documents/Virtual Drum/Kits, or $VDRUM_KITS_DIR).
//
// A sample kit folder holds one audio file per pad (WAV, AIFF or FLAC), named after the pad:
//   hihat, snare, hightom, midtom, floortom, crash, ride   (e.g. "snare.wav")
// Common aliases are accepted (hh, tom1, tom2, tom3, floor...). Missing pads use the Studio
// sounds. Files are mixed to mono, resampled to the device rate and normalised.

#include "DrumSynth.h"
#include "Sampler.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <vector>

namespace vdrum
{

struct KitEntry
{
    juce::String name;
    bool synthesised = true;
    SynthKit synth = SynthKit::studio;
    juce::File folder; // sample kits only
};

class KitLibrary
{
public:
    KitLibrary();

    static juce::File userKitsFolder();

    // Rebuilds the list: synthesised kits first, then sample kits sorted by name.
    void rescan();
    const std::vector<KitEntry>& kits() const noexcept { return entries; }

    // Index of the kit with this name, or 0 (Studio).
    int indexOf (const juce::String& name) const;

    // Sounds of a kit at a given sample rate. Never empty: falls back to Studio per pad.
    SampleBank load (const KitEntry& kit, double sampleRate);

    // Creates the user kits folder with a short README, and returns it.
    juce::File createUserKitsFolder();

private:
    std::vector<float> loadFile (const juce::File& file, double sampleRate);

    juce::AudioFormatManager formats;
    std::vector<KitEntry> entries;
};

} // namespace vdrum
