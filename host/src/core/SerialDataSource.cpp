#include "SerialDataSource.h"

#include <array>

namespace vdrum
{

namespace
{

constexpr int kReadTimeoutMs = 50; // bounds the latency of stop()

} // namespace

SerialDataSource::SerialDataSource (std::string portPath)
    : requestedPath (std::move (portPath))
{
}

SerialDataSource::~SerialDataSource()
{
    stop();
}

std::string SerialDataSource::getName() const
{
    const auto& path = openedPath.empty() ? requestedPath : openedPath;
    return path.empty() ? std::string ("Serial (auto-detect)") : "Serial " + path;
}

bool SerialDataSource::start (Listener& listener, std::string& error)
{
    stop();

    auto path = requestedPath;
    if (path.empty())
    {
        const auto receiver = findReceiverPort();
        if (! receiver.has_value())
        {
            error = "Virtual Drum receiver not found (no serial port with the expected USB ids)";
            return false;
        }
        path = receiver->path;
    }

    if (! port.open (path, error))
        return false;

    openedPath = path;
    parser.reset();
    {
        const std::lock_guard<std::mutex> lock (statsMutex);
        stats = {};
    }

    stopRequested = false;
    running = true;
    thread = std::thread ([this, &listener] { run (listener); });
    return true;
}

void SerialDataSource::stop()
{
    stopRequested = true;
    if (thread.joinable())
        thread.join();

    port.close();
    running = false;
}

bool SerialDataSource::isRunning() const
{
    return running;
}

protocol::FrameParser::Stats SerialDataSource::getStats() const
{
    const std::lock_guard<std::mutex> lock (statsMutex);
    return stats;
}

void SerialDataSource::run (Listener& listener)
{
    std::array<std::uint8_t, 1024> chunk {};

    while (! stopRequested)
    {
        const auto bytesRead = port.read (chunk.data(), chunk.size(), kReadTimeoutMs);
        if (bytesRead < 0)
        {
            running = false;
            listener.sourceFailed ("Serial port " + openedPath + " disconnected or failed");
            return;
        }

        if (bytesRead == 0)
            continue;

        parser.feed (chunk.data(), static_cast<std::size_t> (bytesRead),
                     [&listener] (const protocol::Frame& frame) { listener.frameReceived (frame); });

        const std::lock_guard<std::mutex> lock (statsMutex);
        stats = parser.getStats();
    }
}

} // namespace vdrum
