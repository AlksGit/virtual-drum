#include "FakeDataSource.h"

#include <chrono>
#include <exception>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <timeapi.h>
#endif

namespace vdrum
{

namespace
{

constexpr auto kTick = std::chrono::milliseconds (1);

// Windows sleeps in steps of ~15.6 ms by default, which would bunch the fake hits.
// Raise the timer resolution to 1 ms while the generator runs.
class TimerResolutionScope
{
public:
#if defined(_WIN32)
    TimerResolutionScope() { timeBeginPeriod (1); }
    ~TimerResolutionScope() { timeEndPeriod (1); }
#else
    TimerResolutionScope() = default;
#endif
    TimerResolutionScope (const TimerResolutionScope&) = delete;
    TimerResolutionScope& operator= (const TimerResolutionScope&) = delete;
};

} // namespace

FakeDataSource::FakeDataSource (FakeDrummerSettings settingsIn)
    : settings (settingsIn), tempo (settingsIn.bpm)
{
}

FakeDataSource::~FakeDataSource()
{
    stop();
}

std::string FakeDataSource::getName() const
{
    return "Fake drummer";
}

bool FakeDataSource::start (Listener& listener, std::string& error)
{
    if (generatorThreadId.load() == std::this_thread::get_id())
    {
        error = "start() cannot be called from the source's own thread";
        return false;
    }

    stop();

    framesDelivered = 0;
    parserErrors = 0;
    stopRequested = false;
    running = true;
    thread = std::thread ([this, &listener] { run (listener); });
    return true;
}

void FakeDataSource::stop()
{
    stopRequested = true;

    if (generatorThreadId.load() == std::this_thread::get_id())
        return; // called from a listener callback: the thread exits by itself

    if (thread.joinable())
        thread.join();

    running = false;
}

bool FakeDataSource::isRunning() const
{
    return running;
}

void FakeDataSource::setTempo (double bpm)
{
    tempo = bpm;
}

double FakeDataSource::getTempo() const
{
    return tempo;
}

protocol::FrameParser::Stats FakeDataSource::getStats() const
{
    protocol::FrameParser::Stats stats;
    stats.frames = framesDelivered;
    stats.lengthErrors = parserErrors;
    return stats;
}

void FakeDataSource::run (Listener& listener)
{
    generatorThreadId = std::this_thread::get_id();

    const TimerResolutionScope timerResolution;
    auto drummerSettings = settings;
    drummerSettings.bpm = tempo;
    FakeDrummer drummer (drummerSettings);
    protocol::FrameParser parser;

    std::vector<protocol::Frame> frames;
    frames.reserve (64);
    std::string failure;

    using Clock = std::chrono::steady_clock;
    const auto startTime = Clock::now();
    auto nextTick = startTime;

    try
    {
        while (! stopRequested)
        {
            drummer.setTempo (tempo);

            const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds> (Clock::now() - startTime).count();
            frames.clear();
            drummer.generateUntil (static_cast<std::uint64_t> (elapsedUs), frames);

            for (const auto& frame : frames)
            {
                const auto bytes = protocol::encodeFrame (frame);
                parser.feed (bytes.data(), bytes.size(), [this, &listener] (const protocol::Frame& decoded)
                {
                    ++framesDelivered;
                    listener.frameReceived (decoded);
                });
            }

            const auto& stats = parser.getStats();
            parserErrors = stats.crcErrors + stats.lengthErrors;

            nextTick += kTick;
            const auto now = Clock::now();
            if (nextTick < now)
                nextTick = now; // fell behind (debugger, heavy load): do not try to catch up in a burst
            std::this_thread::sleep_until (nextTick);
        }
    }
    catch (const std::exception& e)
    {
        failure = std::string ("Listener threw an exception: ") + e.what();
    }
    catch (...)
    {
        failure = "Listener threw an unknown exception";
    }

    running = false;
    if (! failure.empty() && ! stopRequested)
        listener.sourceFailed (failure);

    generatorThreadId = std::thread::id {};
}

} // namespace vdrum
