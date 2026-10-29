#include "FrameParser.h"

namespace vdrum::protocol
{

namespace
{

constexpr std::size_t kLengthOffset = 2;
constexpr std::size_t kRxTimeOffset = 3;
constexpr std::size_t kRssiOffset = 7;

} // namespace

void FrameParser::reset() noexcept
{
    head = 0;
    used = 0;
    stats = {};
}

void FrameParser::discardOneByte() noexcept
{
    ++head;
    ++stats.discardedBytes;
}

void FrameParser::compact() noexcept
{
    if (head == 0)
        return;

    const auto remaining = used - head;
    std::memmove (buffer.data(), buffer.data() + head, remaining);
    head = 0;
    used = remaining;
}

bool FrameParser::extract (Frame& frame)
{
    for (;;)
    {
        // Find the sync sequence.
        while (used - head >= 2 && ! (buffer[head] == USB_SYNC0 && buffer[head + 1] == USB_SYNC1))
            discardOneByte();

        const auto available = used - head;
        if (available < 2)
        {
            // A lone trailing byte can only be kept if it may start a sync sequence.
            if (available == 1 && buffer[head] != USB_SYNC0)
                discardOneByte();
            return false;
        }

        if (available < USB_FRAME_HEADER_SIZE)
            return false;

        const auto* frameStart = buffer.data() + head;
        const auto payloadLength = static_cast<std::size_t> (frameStart[kLengthOffset]);
        const auto* payload = frameStart + USB_FRAME_HEADER_SIZE;
        const auto payloadAvailable = std::min (available - USB_FRAME_HEADER_SIZE, payloadLength);

        const auto lengthCheck = payloadLength == 0
                                   ? LengthCheck::inconsistent
                                   : checkPayloadLength (payload, payloadAvailable, payloadLength);

        if (lengthCheck == LengthCheck::inconsistent)
        {
            ++stats.lengthErrors;
            discardOneByte();
            continue;
        }

        if (lengthCheck == LengthCheck::needMoreData)
            return false;

        const auto frameSize = USB_FRAME_OVERHEAD + payloadLength;
        if (available < frameSize)
            return false;

        const auto expectedCrc = crc8 (frameStart + kLengthOffset, frameSize - kLengthOffset - 1);
        if (frameStart[frameSize - 1] != expectedCrc)
        {
            ++stats.crcErrors;
            discardOneByte();
            continue;
        }

        auto packet = decodePacket (payload, payloadLength);
        if (! packet.has_value())
        {
            // Cannot happen after a consistent length check; treated as a length error.
            ++stats.lengthErrors;
            discardOneByte();
            continue;
        }

        frame.rxTimeUs = static_cast<std::uint32_t> (frameStart[kRxTimeOffset])
                       | (static_cast<std::uint32_t> (frameStart[kRxTimeOffset + 1]) << 8)
                       | (static_cast<std::uint32_t> (frameStart[kRxTimeOffset + 2]) << 16)
                       | (static_cast<std::uint32_t> (frameStart[kRxTimeOffset + 3]) << 24);
        frame.rssi = static_cast<std::int8_t> (frameStart[kRssiOffset]);
        frame.packet = std::move (*packet);

        head += frameSize;
        ++stats.frames;
        return true;
    }
}

} // namespace vdrum::protocol
