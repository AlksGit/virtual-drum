#include "TestFramework.h"

#include "DrumSynth.h"
#include "FakeDrummer.h"
#include "HitQueue.h"
#include "KitFiles.h"
#include "Quaternion.h"
#include "Sampler.h"
#include "SceneState.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <thread>
#include <vector>

using namespace vdrum;

namespace
{

constexpr double kSampleRate = 48000.0;

float peak (const std::vector<float>& data, std::size_t begin = 0, std::size_t end = 0)
{
    end = end == 0 ? data.size() : std::min (end, data.size());
    float p = 0.0f;
    for (std::size_t i = begin; i < end; ++i)
        p = std::max (p, std::abs (data[i]));
    return p;
}

float rms (const std::vector<float>& data, std::size_t begin, std::size_t end)
{
    double sum = 0.0;
    for (std::size_t i = begin; i < end; ++i)
        sum += static_cast<double> (data[i]) * data[i];
    return static_cast<float> (std::sqrt (sum / static_cast<double> (end - begin)));
}

// Bank where every pad plays a constant `level` for `length` samples.
SampleBank constantBank (std::size_t length, float level = 1.0f)
{
    SampleBank bank;
    for (auto& sound : bank)
        sound = std::vector<float> (length, level);
    return bank;
}

} // namespace

VDRUM_TEST (velocityCurveIsMonotonicFromSilenceToFull)
{
    CHECK (Sampler::velocityToGain (0.0f) == 0.0f);
    CHECK (Sampler::velocityToGain (1.0f) == 1.0f);
    float previous = 0.0f;
    for (int v = 1; v <= 127; ++v)
    {
        const auto gain = Sampler::velocityToGain (static_cast<float> (v) / 127.0f);
        CHECK (gain > previous);
        previous = gain;
    }
}

VDRUM_TEST (panFollowsThePadPosition)
{
    float left = 0.0f, right = 0.0f;
    Sampler::padPan (Pad::hiHat, left, right);
    CHECK (left > right);
    Sampler::padPan (Pad::ride, left, right);
    CHECK (right > left);
    Sampler::padPan (Pad::snare, left, right);
    CHECK (std::abs (left - right) < 0.2f);
    CHECK (std::abs (left * left + right * right - 1.0f) < 1.0e-4f); // equal power
}

VDRUM_TEST (aTriggeredVoicePlaysOnceThenStops)
{
    Sampler sampler;
    sampler.setSamples (constantBank (100));
    HitQueue queue;
    std::vector<float> left (256), right (256);

    sampler.trigger (Pad::snare, 1.0f);
    sampler.render (queue, left.data(), right.data(), 256);

    CHECK (left[0] > 0.1f && right[0] > 0.1f);
    CHECK (left[99] == left[0]);
    CHECK (left[100] == 0.0f);
    CHECK (sampler.activeVoiceCount() == 0);
}

VDRUM_TEST (louderHitsAreLouder)
{
    Sampler sampler;
    sampler.setSamples (constantBank (64));
    HitQueue queue;
    std::vector<float> soft (64), loud (64), unused (64);

    sampler.trigger (Pad::snare, 0.3f);
    sampler.render (queue, soft.data(), unused.data(), 64);
    sampler.trigger (Pad::snare, 0.9f);
    sampler.render (queue, loud.data(), unused.data(), 64);

    CHECK (loud[10] > soft[10] * 2.0f);
}

VDRUM_TEST (hitsFromTheQueueStartAtTheNextBlock)
{
    Sampler sampler;
    sampler.setSamples (constantBank (1000));
    HitQueue queue;
    std::vector<float> left (64), right (64);

    sampler.render (queue, left.data(), right.data(), 64);
    CHECK (peak (left) == 0.0f);

    CHECK (queue.push ({ Pad::crash, 1.0f }));
    sampler.render (queue, left.data(), right.data(), 64);
    CHECK (left[0] > 0.0f);
}

VDRUM_TEST (voicesAreStolenWhenAllAreBusy)
{
    Sampler sampler;
    sampler.setSamples (constantBank (100000));
    for (int i = 0; i < Sampler::kMaxVoices + 10; ++i)
        sampler.trigger (Pad::ride, 0.5f);
    CHECK (sampler.activeVoiceCount() == Sampler::kMaxVoices);
}

VDRUM_TEST (outputNeverExceedsFullScale)
{
    Sampler sampler;
    sampler.setSamples (constantBank (512));
    HitQueue queue;
    for (int i = 0; i < Sampler::kMaxVoices; ++i)
        sampler.trigger (Pad::snare, 1.0f);

    std::vector<float> left (512), right (512);
    sampler.render (queue, left.data(), right.data(), 512);
    CHECK (peak (left) <= 1.0f && peak (right) <= 1.0f);
}

VDRUM_TEST (queueKeepsOrderAndReportsWhenFull)
{
    HitQueue queue;
    int pushed = 0;
    while (queue.push ({ Pad::snare, static_cast<float> (pushed) }))
        ++pushed;
    CHECK (pushed == 256);

    HitEvent event;
    for (int i = 0; i < pushed; ++i)
    {
        REQUIRE (queue.pop (event));
        CHECK (event.velocity == static_cast<float> (i));
    }
    CHECK (! queue.pop (event));
}

VDRUM_TEST (queueWorksAcrossThreads)
{
    HitQueue queue;
    static constexpr int kCount = 20000;

    std::thread producer ([&queue]
    {
        for (int i = 0; i < kCount; ++i)
            while (! queue.push ({ Pad::snare, static_cast<float> (i) }))
                std::this_thread::yield();
    });

    int received = 0;
    bool inOrder = true;
    HitEvent event;
    while (received < kCount)
    {
        if (queue.pop (event))
        {
            inOrder = inOrder && event.velocity == static_cast<float> (received);
            ++received;
        }
        else
        {
            std::this_thread::yield();
        }
    }
    producer.join();
    CHECK (inOrder);
}

VDRUM_TEST (synthesisedSoundsAreUsable)
{
    for (const auto& info : kDrumKit)
    {
        const auto sound = synthesiseDrum (info.pad, kSampleRate);
        REQUIRE (sound.size() > static_cast<std::size_t> (0.1 * kSampleRate));
        CHECK (std::abs (peak (sound) - 0.9f) < 0.01f);

        bool finite = true;
        for (const auto s : sound)
            finite = finite && std::isfinite (s);
        CHECK (finite);

        // The sound decays: the last tenth is much quieter than the first.
        const auto tenth = sound.size() / 10;
        CHECK (rms (sound, sound.size() - tenth, sound.size()) < 0.25f * rms (sound, 0, tenth));
        CHECK (sound == synthesiseDrum (info.pad, kSampleRate)); // deterministic
    }
}

VDRUM_TEST (fakeGrooveProducesSoundRightAfterEachHit)
{
    // Offline end to end: fake drummer -> zone of each hit -> queue -> sampler.
    Sampler sampler;
    sampler.setSamples (synthesiseKit (kSampleRate));
    HitQueue queue;
    FakeDrummer drummer ({ 120.0, 4 });

    constexpr int kBlock = 64;
    const auto blockUs = static_cast<std::uint64_t> (1.0e6 * kBlock / kSampleRate);
    std::vector<float> left (kBlock), right (kBlock), output;
    std::vector<std::size_t> hitBlocks;
    std::vector<protocol::Frame> frames;

    for (std::uint64_t t = 0; t < 4'000'000; t += blockUs)
    {
        frames.clear();
        drummer.generateUntil (t, frames);
        for (const auto& frame : frames)
            if (const auto* hit = std::get_if<protocol::HitPacket> (&frame.packet))
            {
                queue.push ({ nearestPad (Quat::fromQ14 (hit->quat)), static_cast<float> (hit->velocity) / 127.0f });
                hitBlocks.push_back (output.size());
            }

        sampler.render (queue, left.data(), right.data(), kBlock);
        output.insert (output.end(), left.begin(), left.end());
    }

    REQUIRE (hitBlocks.size() > 10);
    CHECK (peak (output, 0, hitBlocks.front()) == 0.0f); // silence before the first hit
    for (const auto start : hitBlocks)
        CHECK (peak (output, start, start + kBlock) > 0.01f); // sound within one block of each hit
}

VDRUM_TEST (kitSwapHappensAtTheNextRenderAndStopsVoices)
{
    Sampler sampler;
    sampler.setSamples (constantBank (1000, 1.0f));
    HitQueue queue;
    std::vector<float> left (64), right (64);

    sampler.trigger (Pad::snare, 1.0f);
    auto next = std::make_unique<SampleBank> (constantBank (1000, 0.25f));
    REQUIRE (sampler.requestSamples (next));
    CHECK (next == nullptr); // ownership taken
    CHECK (sampler.activeVoiceCount() == 1); // nothing changes before the audio thread runs

    sampler.render (queue, left.data(), right.data(), 64);
    CHECK (peak (left) == 0.0f); // the playing voice stopped with the old kit
    CHECK (sampler.activeVoiceCount() == 0);

    sampler.trigger (Pad::snare, 1.0f);
    sampler.render (queue, left.data(), right.data(), 64);
    float leftGain = 0.0f, rightGain = 0.0f;
    Sampler::padPan (Pad::snare, leftGain, rightGain);
    CHECK (std::abs (left[0] - std::tanh (0.25f * 0.6f * leftGain)) < 1.0e-5f); // new kit's level

    auto old = sampler.takeRetired();
    REQUIRE (old != nullptr);
    CHECK ((*old)[0].layers[0][0] == 1.0f);
    CHECK (sampler.takeRetired() == nullptr);
}

VDRUM_TEST (kitRequestsWaitUntilTheOldKitIsCollected)
{
    Sampler sampler;
    HitQueue queue;
    std::vector<float> left (16), right (16);

    auto first = std::make_unique<SampleBank> (constantBank (10));
    REQUIRE (sampler.requestSamples (first));

    auto second = std::make_unique<SampleBank> (constantBank (20));
    CHECK (! sampler.requestSamples (second)); // still pending
    CHECK (second != nullptr);                  // left untouched

    sampler.render (queue, left.data(), right.data(), 16);
    CHECK (! sampler.requestSamples (second)); // old bank not collected yet

    CHECK (sampler.takeRetired() != nullptr);
    CHECK (sampler.requestSamples (second));
}

VDRUM_TEST (synthesisedKitsAreUsableAndDistinct)
{
    for (const auto kit : kSynthKits)
        for (const auto& info : kDrumKit)
        {
            const auto sound = synthesiseDrum (info.pad, kSampleRate, kit);
            REQUIRE (sound.size() > static_cast<std::size_t> (0.1 * kSampleRate));
            CHECK (std::abs (peak (sound) - 0.9f) < 0.01f);
            bool finite = true;
            for (const auto s : sound)
                finite = finite && std::isfinite (s);
            CHECK (finite);
            const auto tenth = sound.size() / 10;
            CHECK (rms (sound, sound.size() - tenth, sound.size()) < 0.25f * rms (sound, 0, tenth));
        }

    // Each kit sounds different from the studio kit on every pad.
    for (const auto kit : { SynthKit::rock, SynthKit::jazz, SynthKit::electro })
        for (const auto& info : kDrumKit)
            CHECK (synthesiseDrum (info.pad, kSampleRate, kit) != synthesiseDrum (info.pad, kSampleRate, SynthKit::studio));
}

VDRUM_TEST (velocityPicksTheLayerOfMultiSampledPads)
{
    std::size_t layer = 99;
    float gain = 0.0f;

    Sampler::chooseLayer (1, 0.5f, layer, gain); // single layer: classic velocity curve
    CHECK (layer == 0);
    CHECK (std::abs (gain - Sampler::velocityToGain (0.5f)) < 1.0e-6f);

    Sampler::chooseLayer (4, 0.0f, layer, gain);
    CHECK (layer == 0);
    Sampler::chooseLayer (4, 0.30f, layer, gain);
    CHECK (layer == 1);
    Sampler::chooseLayer (4, 1.0f, layer, gain);
    CHECK (layer == 3);
    CHECK (gain > 0.84f && gain <= 1.0f);
}

VDRUM_TEST (multiSampledPadsPlayTheChosenLayerAtItsOwnLevel)
{
    SampleBank bank;
    PadSound snare;
    snare.layers = { std::vector<float> (64, 0.2f), std::vector<float> (64, 0.8f) }; // soft, loud
    bank[static_cast<std::size_t> (Pad::snare)] = snare;

    Sampler sampler;
    sampler.setSamples (std::move (bank));
    HitQueue queue;
    std::vector<float> soft (64), loud (64), unused (64);

    sampler.trigger (Pad::snare, 0.2f);
    sampler.render (queue, soft.data(), unused.data(), 64);
    sampler.trigger (Pad::snare, 0.95f);
    sampler.render (queue, loud.data(), unused.data(), 64);

    // The loud layer is four times louder; gains inside layers only vary between 0.85 and 1.
    CHECK (loud[0] > 3.0f * soft[0]);
}

VDRUM_TEST (kitFileNamesGiveThePadLayer)
{
    CHECK (padLayerFromStem ("snare", "snare") == 0);
    CHECK (padLayerFromStem ("snarev1", "snare") == 1);
    CHECK (padLayerFromStem ("snarev12", "snare") == 12);
    CHECK (padLayerFromStem ("snarev", "snare") == -1);
    CHECK (padLayerFromStem ("snarevx", "snare") == -1);
    CHECK (padLayerFromStem ("snaretop", "snare") == -1);
    CHECK (padLayerFromStem ("floortomv2", "floor") == -1); // "floor" is another name of the pad
    CHECK (padLayerFromStem ("floortomv2", "floortom") == 2);
    CHECK (padLayerFromStem ("sn", "snare") == -1);
}

VDRUM_TEST (aPadKeepsAFewVoicesAndFadesTheOldestOut)
{
    Sampler sampler;
    sampler.setSamples (constantBank (100000));
    HitQueue queue;
    std::vector<float> left (256), right (256);

    for (int i = 0; i < Sampler::kMaxVoicesPerPad + 3; ++i)
        sampler.trigger (Pad::ride, 0.5f);
    CHECK (sampler.activeVoiceCount (Pad::ride) == Sampler::kMaxVoicesPerPad);
    CHECK (sampler.activeVoiceCount() == Sampler::kMaxVoicesPerPad + 3); // 3 still fading

    sampler.render (queue, left.data(), right.data(), Sampler::kReleaseSamples + 1);
    CHECK (sampler.activeVoiceCount() == Sampler::kMaxVoicesPerPad); // fades done

    sampler.trigger (Pad::snare, 0.5f); // other pads are not affected
    CHECK (sampler.activeVoiceCount (Pad::snare) == 1);
}

VDRUM_TEST (releasedVoicesFadeInsteadOfCutting)
{
    Sampler sampler;
    sampler.setSamples (constantBank (100000));
    HitQueue queue;
    std::vector<float> left (Sampler::kReleaseSamples), right (Sampler::kReleaseSamples);

    for (int i = 0; i < Sampler::kMaxVoicesPerPad; ++i)
        sampler.trigger (Pad::crash, 0.5f);
    std::vector<float> before (Sampler::kReleaseSamples), unused (Sampler::kReleaseSamples);
    sampler.render (queue, before.data(), unused.data(), Sampler::kReleaseSamples);

    sampler.trigger (Pad::crash, 0.5f); // releases the oldest, starts a new one: same level
    sampler.render (queue, left.data(), right.data(), Sampler::kReleaseSamples);

    // The total moves smoothly: the released voice loses at most 1/kReleaseSamples per sample.
    float largestStep = 0.0f;
    for (std::size_t i = 1; i < left.size(); ++i)
        largestStep = std::max (largestStep, std::abs (left[i] - left[i - 1]));
    CHECK (largestStep < 0.02f);
    // Once the fade is over, four voices sound again: the same level as before the hit.
    CHECK (std::abs (left.back() - before.back()) < 0.05f);
}

VDRUM_TEST (setSamplesDropsAStalePendingChange)
{
    Sampler sampler;
    auto stale = std::make_unique<SampleBank> (constantBank (10, 0.9f));
    CHECK (sampler.requestSamples (stale));

    sampler.setSamples (constantBank (10, 0.1f)); // device restarted with the current kit
    HitQueue queue;
    std::vector<float> left (8), right (8);
    sampler.trigger (Pad::snare, 1.0f);
    sampler.render (queue, left.data(), right.data(), 8);
    CHECK (sampler.takeRetired() == nullptr); // no swap happened
}
