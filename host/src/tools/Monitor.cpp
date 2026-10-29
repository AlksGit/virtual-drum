// vdrum_monitor: lists serial ports, or prints the frames received from the receiver.
//
//   vdrum_monitor --list         list serial ports with their USB ids
//   vdrum_monitor                auto-detect the receiver and print frames
//   vdrum_monitor --port COM5    print frames from a given port

#include "SerialDataSource.h"
#include "SerialPort.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
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

class PrintingListener final : public vdrum::DataSource::Listener
{
public:
    void frameReceived (const vdrum::protocol::Frame& frame) override
    {
        using namespace vdrum::protocol;

        const std::lock_guard<std::mutex> lock (outputMutex);
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

    void sourceFailed (const std::string& message) override
    {
        const std::lock_guard<std::mutex> lock (outputMutex);
        std::printf ("Source stopped: %s\n", message.c_str());
        quitRequested = true;
    }

private:
    std::mutex outputMutex;
};

} // namespace

int main (int argc, char** argv)
{
    std::string portPath;

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

        std::printf ("Usage: vdrum_monitor [--list] [--port <path>]\n");
        return 2;
    }

    std::signal (SIGINT, onSignal);

    vdrum::SerialDataSource source (portPath);
    PrintingListener listener;
    std::string error;
    if (! source.start (listener, error))
    {
        std::printf ("%s\n\nAvailable ports:\n", error.c_str());
        printPorts();
        return 1;
    }

    std::printf ("Reading from %s (Ctrl+C to stop)\n", source.getName().c_str());
    while (! quitRequested)
        std::this_thread::sleep_for (std::chrono::milliseconds (100));

    source.stop();

    const auto stats = source.getStats();
    std::printf ("\nframes=%llu crc_errors=%llu length_errors=%llu discarded_bytes=%llu\n",
                 static_cast<unsigned long long> (stats.frames), static_cast<unsigned long long> (stats.crcErrors),
                 static_cast<unsigned long long> (stats.lengthErrors), static_cast<unsigned long long> (stats.discardedBytes));
    return 0;
}
