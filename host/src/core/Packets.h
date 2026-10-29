#pragma once

// C++ view of the packets defined in firmware/common/protocol.h.
// Decoding is done field by field (little-endian), never by casting buffers.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

extern "C" {
#include "protocol.h"
}

namespace vdrum::protocol
{

struct Header
{
    std::uint8_t type = 0;
    std::uint8_t stickId = 0;
    std::uint16_t seq = 0;
};

struct ImuSample
{
    std::int16_t ax = 0, ay = 0, az = 0;
    std::int16_t gx = 0, gy = 0, gz = 0;
};

// Quaternion components w, x, y, z in Q14 fixed point (1.0 = QUAT_Q14_ONE).
using QuatQ14 = std::array<std::int16_t, 4>;

struct RawPacket
{
    Header header;
    std::uint32_t t0Us = 0;
    std::uint8_t count = 0;
    std::array<ImuSample, RAW_MAX_SAMPLES> samples {};
};

struct HitPacket
{
    Header header;
    std::uint32_t tUs = 0;
    std::uint8_t velocity = 0;
    QuatQ14 quat {};
    std::uint8_t flags = 0;
};

struct OrientPacket
{
    Header header;
    std::uint32_t tUs = 0;
    QuatQ14 quat {};
};

struct StatusPacket
{
    Header header;
    std::uint16_t vbatMv = 0;
    std::array<std::int16_t, 3> gyroBias {};
    std::uint8_t fwVersion = 0;
};

using Packet = std::variant<RawPacket, HitPacket, OrientPacket, StatusPacket>;

// One USB CDC frame from the receiver.
struct Frame
{
    std::uint32_t rxTimeUs = 0;
    std::int8_t rssi = 0;
    Packet packet;
};

constexpr std::size_t kMaxPayloadSize = 255;
constexpr std::size_t kMaxFrameSize = USB_FRAME_OVERHEAD + kMaxPayloadSize;

enum class LengthCheck
{
    needMoreData, // not enough payload bytes yet to tell
    consistent,   // declared length matches the packet type
    inconsistent  // unknown type, invalid sample count, or length mismatch
};

// Checks a frame's declared payload length against the packet type, using the first
// `available` payload bytes. Lets the parser reject a false sync early.
LengthCheck checkPayloadLength (const std::uint8_t* payload, std::size_t available, std::size_t declaredLength);

// Returns nullopt if the type is unknown or the size does not match the type.
std::optional<Packet> decodePacket (const std::uint8_t* data, std::size_t size);

std::vector<std::uint8_t> encodePacket (const Packet& packet);

std::vector<std::uint8_t> encodeFrame (const Frame& frame);

std::uint8_t crc8 (const std::uint8_t* data, std::size_t size);

// Quaternion helpers.
float q14ToFloat (std::int16_t value);
std::int16_t floatToQ14 (float value);

} // namespace vdrum::protocol
