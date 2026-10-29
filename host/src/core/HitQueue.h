#pragma once

// Single-producer / single-consumer lock-free queue of hits, from the data source thread
// (producer) to the audio thread (consumer). Fixed capacity, no allocation, wait-free.

#include "DrumKit.h"

#include <array>
#include <atomic>
#include <cstddef>

namespace vdrum
{

struct HitEvent
{
    Pad pad = Pad::snare;
    float velocity = 0.0f; // 0..1
};

template <std::size_t Capacity>
class SpscQueue
{
    static_assert ((Capacity & (Capacity - 1)) == 0, "capacity must be a power of two");

public:
    // Producer thread only. Returns false (and drops the hit) when the queue is full.
    bool push (const HitEvent& event) noexcept
    {
        const auto write = writeIndex.load (std::memory_order_relaxed);
        if (write - readIndex.load (std::memory_order_acquire) == Capacity)
            return false;

        slots[write & (Capacity - 1)] = event;
        writeIndex.store (write + 1, std::memory_order_release);
        return true;
    }

    // Consumer thread only.
    bool pop (HitEvent& event) noexcept
    {
        const auto read = readIndex.load (std::memory_order_relaxed);
        if (read == writeIndex.load (std::memory_order_acquire))
            return false;

        event = slots[read & (Capacity - 1)];
        readIndex.store (read + 1, std::memory_order_release);
        return true;
    }

private:
    std::array<HitEvent, Capacity> slots {};
    std::atomic<std::size_t> writeIndex { 0 };
    std::atomic<std::size_t> readIndex { 0 };
};

using HitQueue = SpscQueue<256>;

} // namespace vdrum
