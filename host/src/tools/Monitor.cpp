// vdrum_monitor: lists serial ports, or reports what the receiver sends.
//
//   vdrum_monitor --list            list serial ports with their USB ids
//   vdrum_monitor                   auto-detect the receiver, print a summary every second
//   vdrum_monitor --port COM5       same, on a given port
//   vdrum_monitor --fake [bpm]      same, with the simulated drummer instead of the receiver
//   vdrum_monitor --verbose         also print every frame (slow consoles may then delay
//                                   the reading thread and distort loss measurements)

#include "FakeDataSource.h"
#include "SerialDataSource.h"
#include "SerialPort.h"

#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>

namespace
{

std::atomic<bool> quitRequested { false };

void onSignal (int)
{
    quitRequested = true;
}

void printPorts()
{
    const auto ports = vdrum::listSerialPorts();
    if (ports.empty())
        std::printf ("No serial port found.\n");

    for (const auto& port : ports)
    {
        if (port.hasUsbIds)
            std::printf ("%-24s %04X:%04X  %s\n", port.path.c_str(), port.vendorId, port.productId, port.description.c_str());
        else
            std::printf ("%-24s (no USB ids)  %s\n", port.path.c_str(), port.description.c_str());
    }
}

void printFrame (const vdrum::protocol::Frame& frame)
{
    using namespace vdrum::protocol;

    std::printf ("rx=%10u rssi=%4d ", static_cast<unsigned> (frame.rxTimeUs), frame.rssi);

    std::visit ([] (const auto& packet)
    {
        using T = std::decay_t<decltype (packet)>;
        std::printf ("stick=%u seq=%5u ", packet.header.stickId, packet.header.seq);

        if constexpr (std::is_same_v<T, RawPacket>)
            std::printf ("RAW    t0=%u n=%u a=(%d,%d,%d) g=(%d,%d,%d)\n", static_cast<unsigned> (packet.t0Us), packet.count,
                         packet.samples[0].ax, packet.samples[0].ay, packet.samples[0].az,
                         packet.samples[0].gx, packet.samples[0].gy, packet.samples[0].gz);
        else if constexpr (std::is_same_v<T, HitPacket>)
            std::printf ("HIT    t=%u vel=%u flags=0x%02X\n", static_cast<unsigned> (packet.tUs), packet.velocity, packet.flags);
        else if constexpr (std::is_same_v<T, OrientPacket>)
            std::printf ("ORIENT t=%u q=(%.3f,%.3f,%.3f,%.3f)\n", static_cast<unsigned> (packet.tUs),
                         static_cast<double> (q14ToFloat (packet.quat[0])), static_cast<double> (q14ToFloat (packet.quat[1])),
                         static_cast<double> (q14ToFloat (packet.quat[2])), static_cast<double> (q14ToFloat (packet.quat[3])));
        else
            std::printf ("STATUS vbat=%umV fw=%u\n", packet.vbatMv, packet.fwVersion);
    }, frame.packet);
}

constexpr std::size_t kMaxSticks = 2;
constexpr std::size_t kPacketKinds = 4; // RAW, HIT, ORIENT, STATUS (variant order)

// Counts frames per stick and per type, and packets lost from gaps in seq.
// Runs on the reader thread and only touches atomics, so it never blocks it.
class CountingListener final : public vdrum::DataSource::Listener
{
public:
    explicit CountingListener (bool verboseIn) : verbose (verboseIn) {}

    void frameReceived (const vdrum::protocol::Frame& frame) override
    {
        const auto& header = std::visit ([] (const auto& packet) -> const vdrum::protocol::Header& { return packet.header; },
                                         frame.packet);
        if (header.stickId < kMaxSticks)
        {
            auto& stick = sticks[header.stickId];
            stick.counts[frame.packet.index()].fetch_add (1, std::memory_order_relaxed);

            if (stick.hasSeq)
            {
                const auto gap = static_cast<std::uint16_t> (header.seq - stick.lastSeq - 1);
                stick.lost.fetch_add (gap, std::memory_order_relaxed);
            }
            stick.lastSeq = header.seq;
            stick.hasSeq = true;
        }

        if (verbose)
        {
            const std::lock_guard<std::mutex> lock (outputMutex);
            printFrame (frame);
        }
    }

    void sourceFailed (const std::string& message) override
    {
        const std::lock_guard<std::mutex> lock (outputMutex);
        std::printf ("Source stopped: %s\n", message.c_str());
        quitRequested = true;
    }

    // Prints and resets the counters of the last interval.
    void printSummary (double seconds)
    {
        static const char* const kNames[kPacketKinds] = { "raw", "hit", "orient", "status" };

        const std::lock_guard<std::mutex> lock (outputMutex);
        for (std::size_t id = 0; id < kMaxSticks; ++id)
        {
            auto& stick = sticks[id];
            std::printf ("stick %zu:", id);
            for (std::size_t kind = 0; kind < kPacketKinds; ++kind)
                std::printf (" %s=%.0f/s", kNames[kind], static_cast<double> (stick.counts[kind].exchange (0)) / seconds);
            std::printf ("  lost=%llu\n", static_cast<unsigned long long> (stick.lost.exchange (0)));
        }
        std::fflush (stdout); // keep the output live when redirected to a file
    }

private:
    struct StickCounters
    {
        std::array<std::atomic<std::uint64_t>, kPacketKinds> counts {};
        std::atomic<std::uint64_t> lost { 0 };
        std::uint16_t lastSeq = 0; // reader thread only
        bool hasSeq = false;       // reader thread only
    };

    const bool verbose;
    std::array<StickCounters, kMaxSticks> sticks {};
    std::mutex outputMutex;
};

} // namespace

int main (int argc, char** argv)
{
    std::string portPath;
    bool verbose = false;
    bool fake = false;
    double fakeBpm = 96.0;

    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp (argv[i], "--list") == 0)
        {
            printPorts();
            return 0;
        }

        if (std::strcmp (argv[i], "--port") == 0 && i + 1 < argc)
        {
            portPath = argv[++i];
            continue;
        }

        if (std::strcmp (argv[i], "--verbose") == 0)
        {
            verbose = true;
            continue;
        }

        if (std::strcmp (argv[i], "--fake") == 0)
        {
            fake = true;
            if (i + 1 < argc && std::atof (argv[i + 1]) > 0.0)
                fakeBpm = std::atof (argv[++i]);
            continue;
        }

        std::printf ("Usage: vdrum_monitor [--list] [--port <path> | --fake [bpm]] [--verbose]\n");
        return 2;
    }

    std::signal (SIGINT, onSignal);

    std::unique_ptr<vdrum::DataSource> source;
    if (fake)
        source = std::make_unique<vdrum::FakeDataSource> (vdrum::FakeDrummerSettings { fakeBpm, 1 });
    else
        source = std::make_unique<vdrum::SerialDataSource> (portPath);

    CountingListener listener (verbose);
    std::string error;
    if (! source->start (listener, error))
    {
        std::printf ("%s\n\nAvailable ports:\n", error.c_str());
        printPorts();
        return 1;
    }

    std::printf ("Reading from %s (Ctrl+C to stop)\n", source->getName().c_str());
    std::fflush (stdout);

    using Clock = std::chrono::steady_clock;
    auto lastSummary = Clock::now();
    while (! quitRequested)
    {
        std::this_thread::sleep_for (std::chrono::milliseconds (100));

        const auto now = Clock::now();
        if (now - lastSummary >= std::chrono::seconds (1))
        {
            listener.printSummary (std::chrono::duration<double> (now - lastSummary).count());
            lastSummary = now;
        }
    }

    source->stop();

    if (const auto* serial = dynamic_cast<const vdrum::SerialDataSource*> (source.get()))
    {
        const auto stats = serial->getStats();
        std::printf ("\nframes=%llu crc_errors=%llu length_errors=%llu discarded_bytes=%llu\n",
                     static_cast<unsigned long long> (stats.frames), static_cast<unsigned long long> (stats.crcErrors),
                     static_cast<unsigned long long> (stats.lengthErrors), static_cast<unsigned long long> (stats.discardedBytes));
    }
    return 0;
}
