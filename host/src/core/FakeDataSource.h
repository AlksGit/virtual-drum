#pragma once

#include "DataSource.h"
#include "FakeDrummer.h"
#include "FrameParser.h"

#include <atomic>
#include <thread>

namespace vdrum
{

// Plays a FakeDrummer in real time on its own thread. Every frame goes through the real
// wire format: it is encoded to bytes, then decoded by a FrameParser, exactly as frames
// coming from the USB receiver.
class FakeDataSource final : public DataSource
{
public:
    explicit FakeDataSource (FakeDrummerSettings settings = {});
    ~FakeDataSource() override;

    std::string getName() const override;
    bool start (Listener& listener, std::string& error) override;
    void stop() override;
    bool isRunning() const override;

    // Thread-safe; applies from the next bar.
    void setTempo (double bpm);
    double getTempo() const;

    protocol::FrameParser::Stats getStats() const;

private:
    void run (Listener& listener);

    FakeDrummerSettings settings;
    std::atomic<double> tempo;
    std::thread thread;
    std::atomic<std::thread::id> generatorThreadId {};
    std::atomic<bool> running { false };
    std::atomic<bool> stopRequested { false };

    std::atomic<std::uint64_t> framesDelivered { 0 };
    std::atomic<std::uint64_t> parserErrors { 0 };
};

} // namespace vdrum
