#pragma once

#include "Packets.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace vdrum::protocol
{

// Incremental parser for the receiver's USB CDC byte stream (see docs/PROTOCOL.md).
// Bytes can be fed in chunks of any size; frames split across chunks are reassembled.
// On a bad crc, an inconsistent length or an unknown type, the parser drops one byte
// and resynchronises on the next 0xA5 0x5A.
// Not thread-safe: feed it from a single thread.
class FrameParser
{
public:
    struct Stats
    {
        std::uint64_t frames = 0;
        std::uint64_t crcErrors = 0;
        std::uint64_t lengthErrors = 0;  // inconsistent length / unknown type after a sync
        std::uint64_t discardedBytes = 0;
    };

    // Calls onFrame (const Frame&) for every valid frame, in stream order.
    template <typename OnFrame>
    void feed (const std::uint8_t* data, std::size_t size, OnFrame&& onFrame)
    {
        while (size > 0)
        {
            const auto chunk = std::min (size, buffer.size() - used);
            std::memcpy (buffer.data() + used, data, chunk);
            used += chunk;
            data += chunk;
            size -= chunk;

            Frame frame;
            while (extract (frame))
                onFrame (frame);

            compact();
        }
    }

    const Stats& getStats() const noexcept { return stats; }

    void reset() noexcept;

private:
    // Extracts the next frame starting at `head`; returns false when more data is needed.
    bool extract (Frame& frame);
    void discardOneByte() noexcept;
    void compact() noexcept;

    // Two maximum-size frames: after extraction at most one partial frame remains, so there
    // is always room for new bytes.
    std::array<std::uint8_t, 2 * kMaxFrameSize> buffer {};
    std::size_t head = 0;
    std::size_t used = 0;
    Stats stats;
};

} // namespace vdrum::protocol
