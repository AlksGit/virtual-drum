#include "TestFramework.h"

#include "FrameParser.h"
#include "Packets.h"

#include <cstring>

using namespace vdrum::protocol;

namespace
{

// Reference frame from docs/PROTOCOL.md (crc computed independently, in Python).
const std::vector<std::uint8_t> kReferenceFrame {
    0xA5, 0x5A, 0x12, 0xDD, 0xCC, 0xBB, 0xAA, 0xD8,
    0x02, 0x01, 0x34, 0x12, 0x04, 0x03, 0x02, 0x01, 0x64,
    0x00, 0x40, 0xFF, 0xFF, 0x00, 0x00, 0x02, 0x01, 0x01,
    0x32
};

Frame makeOrientFrame (std::uint16_t seq)
{
    OrientPacket packet;
    packet.header.stickId = 0;
    packet.header.seq = seq;
    packet.tUs = 1000u * seq;
    packet.quat = { QUAT_Q14_ONE, 0, 0, 0 };

    Frame frame;
    frame.rxTimeUs = 5000u + seq;
    frame.rssi = -50;
    frame.packet = packet;
    return frame;
}

std::vector<Frame> parseAll (FrameParser& parser, const std::vector<std::uint8_t>& bytes)
{
    std::vector<Frame> frames;
    parser.feed (bytes.data(), bytes.size(), [&frames] (const Frame& frame) { frames.push_back (frame); });
    return frames;
}

std::uint16_t seqOf (const Frame& frame)
{
    return std::visit ([] (const auto& packet) { return packet.header.seq; }, frame.packet);
}

void append (std::vector<std::uint8_t>& out, const std::vector<std::uint8_t>& bytes)
{
    out.insert (out.end(), bytes.begin(), bytes.end());
}

} // namespace

VDRUM_TEST (crc8MatchesTheCheckValue)
{
    const char* check = "123456789";
    CHECK (crc8 (reinterpret_cast<const std::uint8_t*> (check), std::strlen (check)) == 0xF4);
}

VDRUM_TEST (referenceFrameDecodesToTheDocumentedValues)
{
    FrameParser parser;
    const auto frames = parseAll (parser, kReferenceFrame);
    REQUIRE (frames.size() == 1);

    const auto& frame = frames.front();
    CHECK (frame.rxTimeUs == 0xAABBCCDDu);
    CHECK (frame.rssi == -40);

    const auto* hit = std::get_if<HitPacket> (&frame.packet);
    REQUIRE (hit != nullptr);
    CHECK (hit->header.type == PKT_HIT);
    CHECK (hit->header.stickId == 1);
    CHECK (hit->header.seq == 0x1234);
    CHECK (hit->tUs == 0x01020304u);
    CHECK (hit->velocity == 100);
    CHECK (hit->quat[0] == 16384);
    CHECK (hit->quat[1] == -1);
    CHECK (hit->quat[2] == 0);
    CHECK (hit->quat[3] == 258);
    CHECK (hit->flags == HIT_FLAG_GYRO_SAT);
}

VDRUM_TEST (encodingTheReferencePacketGivesTheReferenceFrame)
{
    HitPacket hit;
    hit.header.stickId = 1;
    hit.header.seq = 0x1234;
    hit.tUs = 0x01020304u;
    hit.velocity = 100;
    hit.quat = { 16384, -1, 0, 258 };
    hit.flags = HIT_FLAG_GYRO_SAT;

    Frame frame;
    frame.rxTimeUs = 0xAABBCCDDu;
    frame.rssi = -40;
    frame.packet = hit;

    CHECK (encodeFrame (frame) == kReferenceFrame);
}

VDRUM_TEST (everyPacketTypeRoundTrips)
{
    RawPacket raw;
    raw.header = { PKT_RAW, 1, 7 };
    raw.t0Us = 123456;
    raw.count = 1;
    raw.samples[0] = { -16000, 2, 3, 32767, -32768, 6 };

    StatusPacket status;
    status.header = { PKT_STATUS, 0, 9 };
    status.vbatMv = 3712;
    status.gyroBias = { -3, 0, 12 };
    status.fwVersion = 4;

    HitPacket hit;
    hit.header = { PKT_HIT, 1, 8 };
    hit.tUs = 99;
    hit.velocity = 127;
    hit.quat = { 1, -2, 3, -4 };
    hit.flags = HIT_FLAG_GYRO_SAT | HIT_FLAG_ACCEL_SAT;

    const std::vector<Packet> packets { raw, hit, makeOrientFrame (3).packet, status };

    for (const auto& packet : packets)
    {
        const auto bytes = encodePacket (packet);
        const auto decoded = decodePacket (bytes.data(), bytes.size());
        REQUIRE (decoded.has_value());
        CHECK (decoded->index() == packet.index());
        CHECK (encodePacket (*decoded) == bytes);
    }

    const auto rawBytes = encodePacket (raw);
    CHECK (rawBytes.size() == sizeof (pkt_raw));
    const auto decodedRaw = std::get<RawPacket> (*decodePacket (rawBytes.data(), rawBytes.size()));
    CHECK (decodedRaw.samples[0].ax == -16000);
    CHECK (decodedRaw.samples[0].gx == 32767);
    CHECK (decodedRaw.samples[0].gy == -32768);
}

VDRUM_TEST (encodedSizesMatchTheCStructs)
{
    CHECK (encodePacket (HitPacket {}).size() == sizeof (pkt_hit));
    CHECK (encodePacket (OrientPacket {}).size() == sizeof (pkt_orient));
    CHECK (encodePacket (StatusPacket {}).size() == sizeof (pkt_status));

    RawPacket raw;
    raw.count = 1;
    CHECK (encodePacket (raw).size() == sizeof (pkt_raw));
}

VDRUM_TEST (decodeRejectsUnknownTypeAndWrongSize)
{
    auto bytes = encodePacket (makeOrientFrame (1).packet);
    CHECK (decodePacket (bytes.data(), bytes.size() - 1) == std::nullopt);

    bytes[0] = 0x7F;
    CHECK (decodePacket (bytes.data(), bytes.size()) == std::nullopt);

    RawPacket raw;
    raw.count = 1;
    auto rawBytes = encodePacket (raw);
    rawBytes[8] = 0; // sample count 0 is invalid
    CHECK (decodePacket (rawBytes.data(), rawBytes.size()) == std::nullopt);
}

VDRUM_TEST (framesSplitByteByByteAreReassembled)
{
    std::vector<std::uint8_t> stream;
    for (std::uint16_t seq = 0; seq < 5; ++seq)
        append (stream, encodeFrame (makeOrientFrame (seq)));

    FrameParser parser;
    std::vector<Frame> frames;
    for (const auto byte : stream)
        parser.feed (&byte, 1, [&frames] (const Frame& frame) { frames.push_back (frame); });

    REQUIRE (frames.size() == 5);
    for (std::uint16_t seq = 0; seq < 5; ++seq)
        CHECK (seqOf (frames[seq]) == seq);
    CHECK (parser.getStats().discardedBytes == 0);
}

VDRUM_TEST (leadingGarbageIsSkipped)
{
    std::vector<std::uint8_t> stream { 0x00, 0xA5, 0x13, 0x5A, 0xA5, 0xFF };
    append (stream, kReferenceFrame);

    FrameParser parser;
    const auto frames = parseAll (parser, stream);
    CHECK (frames.size() == 1);
    CHECK (parser.getStats().discardedBytes == 6);
}

VDRUM_TEST (corruptedFrameIsDroppedAndTheNextOneIsKept)
{
    auto corrupted = encodeFrame (makeOrientFrame (1));
    corrupted[12] ^= 0x01; // flip a payload bit

    std::vector<std::uint8_t> stream;
    append (stream, corrupted);
    append (stream, encodeFrame (makeOrientFrame (2)));

    FrameParser parser;
    const auto frames = parseAll (parser, stream);
    REQUIRE (frames.size() == 1);
    CHECK (seqOf (frames.front()) == 2);
    CHECK (parser.getStats().crcErrors == 1);
}

VDRUM_TEST (inconsistentLengthIsRejectedWithoutWaiting)
{
    // A false sync followed by a large length must not stall the parser until 255 bytes arrive.
    std::vector<std::uint8_t> stream { 0xA5, 0x5A, 0xF0, 0, 0, 0, 0, 0, PKT_HIT };
    append (stream, kReferenceFrame);

    FrameParser parser;
    const auto frames = parseAll (parser, stream);
    CHECK (frames.size() == 1);
    CHECK (parser.getStats().lengthErrors >= 1);
}

VDRUM_TEST (syncBytesInsidePayloadDoNotBreakParsing)
{
    OrientPacket packet;
    packet.header.seq = 0x5AA5;
    packet.tUs = 0xA55AA55Au;
    packet.quat = { static_cast<std::int16_t> (0x5AA5), 0, 0, 0 };

    Frame frame;
    frame.packet = packet;

    std::vector<std::uint8_t> stream;
    for (int i = 0; i < 3; ++i)
        append (stream, encodeFrame (frame));

    FrameParser parser;
    CHECK (parseAll (parser, stream).size() == 3);
    CHECK (parser.getStats().discardedBytes == 0);
}

VDRUM_TEST (largeChunksAreFullyProcessed)
{
    // More bytes in one call than the internal buffer can hold.
    std::vector<std::uint8_t> stream;
    for (std::uint16_t seq = 0; seq < 200; ++seq)
        append (stream, encodeFrame (makeOrientFrame (seq)));

    FrameParser parser;
    const auto frames = parseAll (parser, stream);
    REQUIRE (frames.size() == 200);
    CHECK (seqOf (frames.back()) == 199);
}

VDRUM_TEST (q14ConversionsAreSymmetric)
{
    CHECK (q14ToFloat (QUAT_Q14_ONE) == 1.0f);
    CHECK (floatToQ14 (1.0f) == QUAT_Q14_ONE);
    CHECK (floatToQ14 (-1.0f) == -QUAT_Q14_ONE);
    CHECK (floatToQ14 (5.0f) == 32767);
    CHECK (floatToQ14 (-5.0f) == -32768);
}

VDRUM_TEST (rawPacketWithInvalidCountStillEncodesADecodableFrame)
{
    RawPacket raw; // count 0 is not allowed on the wire
    const auto bytes = encodePacket (raw);
    const auto decoded = decodePacket (bytes.data(), bytes.size());
    REQUIRE (decoded.has_value());
    CHECK (std::get<RawPacket> (*decoded).count == 1);
}
