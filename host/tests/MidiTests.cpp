#include "TestFramework.h"

#include "MidiMapping.h"

#include <set>

using namespace vdrum;

VDRUM_TEST (padsUseDistinctGeneralMidiDrumNotes)
{
    std::set<int> notes;
    for (const auto& info : kDrumKit)
    {
        const auto note = midi::noteForPad (info.pad);
        CHECK (note >= 35 && note <= 81); // General MIDI percussion key range
        notes.insert (note);
    }
    CHECK (notes.size() == kDrumKit.size());
    CHECK (midi::noteForPad (Pad::snare) == 38);
    CHECK (midi::noteForPad (Pad::hiHat) == 42);
    CHECK (midi::noteForPad (Pad::ride) == 51);
}

VDRUM_TEST (midiVelocityNeverReachesZero)
{
    CHECK (midi::midiVelocity (0.0f) == 1);   // 0 would be a note off
    CHECK (midi::midiVelocity (1.0f) == 127);
    CHECK (midi::midiVelocity (2.0f) == 127);
    CHECK (midi::midiVelocity (100.0f / 127.0f) == 100);
}

VDRUM_TEST (hitMessagesAreNoteOnAndOffOnChannel10)
{
    const auto on = midi::noteOn (Pad::crash, 1.0f);
    CHECK (on[0] == 0x99);
    CHECK (on[1] == 49);
    CHECK (on[2] == 127);

    const auto off = midi::noteOff (Pad::crash);
    CHECK (off[0] == 0x89);
    CHECK (off[1] == 49);
    CHECK (off[2] == 0);
}
