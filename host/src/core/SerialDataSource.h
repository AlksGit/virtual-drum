#pragma once

#include "DataSource.h"
#include "FrameParser.h"
#include "SerialPort.h"

#include <atomic>
#include <mutex>
#include <thread>

namespace vdrum
{

// Reads frames from the USB receiver over a serial port, on a dedicated thread.
class SerialDataSource final : public DataSource
{
public:
    // An empty path means: auto-detect the receiver by its USB ids when starting.
    explicit SerialDataSource (std::string portPath = {});
    ~SerialDataSource() override;

    std::string getName() const override;
    bool start (Listener& listener, std::string& error) override;
    void stop() override;
    bool isRunning() const override;

    // Snapshot of the parser counters (frames, crc errors...), safe from any thread.
    protocol::FrameParser::Stats getStats() const;

private:
    void run (Listener& listener);

    std::string requestedPath;
    std::string openedPath;
    SerialPort port;
    protocol::FrameParser parser;

    std::thread thread;
    std::atomic<bool> running { false };
    std::atomic<bool> stopRequested { false };

    mutable std::mutex statsMutex;
    protocol::FrameParser::Stats stats;
};

} // namespace vdrum
