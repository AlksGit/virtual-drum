#pragma once

#include "Packets.h"

#include <string>

namespace vdrum
{

// A producer of protocol frames: the USB receiver, a fake generator, a recording...
// The rest of the app only sees this interface.
class DataSource
{
public:
    class Listener
    {
    public:
        virtual ~Listener() = default;

        // Called on the source's own thread, in stream order. Must return quickly and
        // must not block (hand the data over to other threads through a queue).
        // An exception thrown here stops the source and is reported via sourceFailed().
        virtual void frameReceived (const protocol::Frame& frame) = 0;

        // Called on the source's own thread when the source stops by itself
        // (device unplugged, read error...). The source is no longer running afterwards.
        // May call stop(), but not start(): restart from another thread. Must not throw.
        virtual void sourceFailed (const std::string& message) = 0;
    };

    virtual ~DataSource() = default;

    virtual std::string getName() const = 0;

    // Starts delivering frames to the listener, which must outlive the running source.
    // Returns false and fills `error` if the source cannot start.
    virtual bool start (Listener& listener, std::string& error) = 0;

    // Stops the source and waits until no more listener calls are in progress.
    // Safe to call when not running.
    virtual void stop() = 0;

    virtual bool isRunning() const = 0;
};

} // namespace vdrum
