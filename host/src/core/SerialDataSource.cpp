#include "SerialDataSource.h"

#include <array>
#include <exception>

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
    std::string path;
    {
        const std::lock_guard<std::mutex> lock (stateMutex);
        path = openedPath.empty() ? requestedPath : openedPath;
    }
    return path.empty() ? std::string ("Serial (auto-detect)") : "Serial " + path;
}

bool SerialDataSource::start (Listener& listener, std::string& error)
{
    if (isReaderThread())
    {
        error = "start() cannot be called from the source's own thread";
        return false;
    }

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

    parser.reset();
    {
        const std::lock_guard<std::mutex> lock (stateMutex);
        openedPath = path;
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

    // Called from a listener callback: joining here would deadlock. The thread exits on its
    // own; the join happens in the next start(), stop() or the destructor.
    if (isReaderThread())
        return;

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
    const std::lock_guard<std::mutex> lock (stateMutex);
    return stats;
}

bool SerialDataSource::isReaderThread() const
{
    return readerThreadId.load() == std::this_thread::get_id();
}

void SerialDataSource::run (Listener& listener)
{
    // Set before any listener call, so that stop() called from a callback sees it.
    readerThreadId = std::this_thread::get_id();

    std::array<std::uint8_t, 1024> chunk {};
    std::string failure;

    try
    {
        while (! stopRequested)
        {
            const auto bytesRead = port.read (chunk.data(), chunk.size(), kReadTimeoutMs);
            if (bytesRead < 0)
            {
                failure = "Serial port disconnected or failed";
                break;
            }

            if (bytesRead == 0)
                continue;

            parser.feed (chunk.data(), static_cast<std::size_t> (bytesRead),
                         [&listener] (const protocol::Frame& frame) { listener.frameReceived (frame); });

            const std::lock_guard<std::mutex> lock (stateMutex);
            stats = parser.getStats();
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

    // Thread ids can be reused once this thread ends: forget it after the last callback.
    readerThreadId = std::thread::id {};
}

} // namespace vdrum
