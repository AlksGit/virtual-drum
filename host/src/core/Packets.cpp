#include "Packets.h"

#include <cmath>
#include <type_traits>

namespace vdrum::protocol
{

namespace
{

constexpr std::size_t kHeaderSize = sizeof (pkt_header);
constexpr std::size_t kSampleSize = sizeof (imu_sample);
constexpr std::size_t kRawFixedSize = sizeof (pkt_raw) - RAW_MAX_SAMPLES * kSampleSize;
constexpr std::size_t kRawCountOffset = kHeaderSize + 4; // header + t0_us

class Reader
{
public:
    explicit Reader (const std::uint8_t* dataIn) : data (dataIn) {}

    std::uint8_t u8() { return data[pos++]; }

    std::uint16_t u16()
    {
        const auto value = static_cast<std::uint16_t> (data[pos] | (data[pos + 1] << 8));
        pos += 2;
        return value;
    }

    std::int16_t i16() { return static_cast<std::int16_t> (u16()); }

    std::uint32_t u32()
    {
        const auto value = static_cast<std::uint32_t> (data[pos])
                         | (static_cast<std::uint32_t> (data[pos + 1]) << 8)
                         | (static_cast<std::uint32_t> (data[pos + 2]) << 16)
                         | (static_cast<std::uint32_t> (data[pos + 3]) << 24);
        pos += 4;
        return value;
    }

private:
    const std::uint8_t* data;
    std::size_t pos = 0;
};

class Writer
{
public:
    explicit Writer (std::vector<std::uint8_t>& outIn) : out (outIn) {}

    void u8 (std::uint8_t value) { out.push_back (value); }
    void i8 (std::int8_t value) { u8 (static_cast<std::uint8_t> (value)); }

    void u16 (std::uint16_t value)
    {
        out.push_back (static_cast<std::uint8_t> (value & 0xffu));
        out.push_back (static_cast<std::uint8_t> (value >> 8));
    }

    void i16 (std::int16_t value) { u16 (static_cast<std::uint16_t> (value)); }

    void u32 (std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
            out.push_back (static_cast<std::uint8_t> ((value >> shift) & 0xffu));
    }

private:
    std::vector<std::uint8_t>& out;
};

Header readHeader (Reader& reader)
{
    Header header;
    header.type = reader.u8();
    header.stickId = reader.u8();
    header.seq = reader.u16();
    return header;
}

void writeHeader (Writer& writer, const Header& header)
{
    writer.u8 (header.type);
    writer.u8 (header.stickId);
    writer.u16 (header.seq);
}

QuatQ14 readQuat (Reader& reader)
{
    QuatQ14 quat {};
    for (auto& component : quat)
        component = reader.i16();
    return quat;
}

void writeQuat (Writer& writer, const QuatQ14& quat)
{
    for (const auto component : quat)
        writer.i16 (component);
}

ImuSample readSample (Reader& reader)
{
    ImuSample sample;
    sample.ax = reader.i16();
    sample.ay = reader.i16();
    sample.az = reader.i16();
    sample.gx = reader.i16();
    sample.gy = reader.i16();
    sample.gz = reader.i16();
    return sample;
}

void writeSample (Writer& writer, const ImuSample& sample)
{
    writer.i16 (sample.ax);
    writer.i16 (sample.ay);
    writer.i16 (sample.az);
    writer.i16 (sample.gx);
    writer.i16 (sample.gy);
    writer.i16 (sample.gz);
}

constexpr std::size_t rawPayloadSize (std::size_t count)
{
    return kRawFixedSize + count * kSampleSize;
}

LengthCheck matches (std::size_t declaredLength, std::size_t expected)
{
    return declaredLength == expected ? LengthCheck::consistent : LengthCheck::inconsistent;
}

} // namespace

LengthCheck checkPayloadLength (const std::uint8_t* payload, std::size_t available, std::size_t declaredLength)
{
    if (available < 1)
        return LengthCheck::needMoreData;

    switch (payload[0])
    {
        case PKT_HIT:    return matches (declaredLength, sizeof (pkt_hit));
        case PKT_ORIENT: return matches (declaredLength, sizeof (pkt_orient));
        case PKT_STATUS: return matches (declaredLength, sizeof (pkt_status));

        case PKT_RAW:
        {
            if (declaredLength <= kRawCountOffset)
                return LengthCheck::inconsistent;
            if (available <= kRawCountOffset)
                return LengthCheck::needMoreData;

            const auto count = static_cast<std::size_t> (payload[kRawCountOffset]);
            if (count < 1 || count > RAW_MAX_SAMPLES)
                return LengthCheck::inconsistent;
            return matches (declaredLength, rawPayloadSize (count));
        }

        default:
            return LengthCheck::inconsistent;
    }
}

std::optional<Packet> decodePacket (const std::uint8_t* data, std::size_t size)
{
    if (data == nullptr || checkPayloadLength (data, size, size) != LengthCheck::consistent)
        return std::nullopt;

    Reader reader (data);
    const auto header = readHeader (reader);

    switch (header.type)
    {
        case PKT_RAW:
        {
            RawPacket packet;
            packet.header = header;
            packet.t0Us = reader.u32();
            packet.count = reader.u8();
            for (std::size_t i = 0; i < packet.count; ++i)
                packet.samples[i] = readSample (reader);
            return packet;
        }

        case PKT_HIT:
        {
            HitPacket packet;
            packet.header = header;
            packet.tUs = reader.u32();
            packet.velocity = reader.u8();
            packet.quat = readQuat (reader);
            packet.flags = reader.u8();
            return packet;
        }

        case PKT_ORIENT:
        {
            OrientPacket packet;
            packet.header = header;
            packet.tUs = reader.u32();
            packet.quat = readQuat (reader);
            return packet;
        }

        case PKT_STATUS:
        {
            StatusPacket packet;
            packet.header = header;
            packet.vbatMv = reader.u16();
            for (auto& bias : packet.gyroBias)
                bias = reader.i16();
            packet.fwVersion = reader.u8();
            return packet;
        }

        default:
            return std::nullopt;
    }
}

std::vector<std::uint8_t> encodePacket (const Packet& packet)
{
    std::vector<std::uint8_t> out;
    out.reserve (32);
    Writer writer (out);

    std::visit ([&writer] (const auto& p)
    {
        using T = std::decay_t<decltype (p)>;

        // The type byte always follows the variant alternative, whatever the header says.
        auto header = p.header;

        if constexpr (std::is_same_v<T, RawPacket>)
        {
            header.type = PKT_RAW;
            writeHeader (writer, header);
            // Keep the count within what the format allows, so an encoded frame always decodes.
            const auto maxCount = static_cast<std::uint8_t> (p.samples.size());
            const auto count = p.count < 1 ? std::uint8_t { 1 } : (p.count > maxCount ? maxCount : p.count);
            writer.u32 (p.t0Us);
            writer.u8 (count);
            for (std::size_t i = 0; i < count; ++i)
                writeSample (writer, p.samples[i]);
        }
        else if constexpr (std::is_same_v<T, HitPacket>)
        {
            header.type = PKT_HIT;
            writeHeader (writer, header);
            writer.u32 (p.tUs);
            writer.u8 (p.velocity);
            writeQuat (writer, p.quat);
            writer.u8 (p.flags);
        }
        else if constexpr (std::is_same_v<T, OrientPacket>)
        {
            header.type = PKT_ORIENT;
            writeHeader (writer, header);
            writer.u32 (p.tUs);
            writeQuat (writer, p.quat);
        }
        else
        {
            static_assert (std::is_same_v<T, StatusPacket>, "unhandled packet type");
            header.type = PKT_STATUS;
            writeHeader (writer, header);
            writer.u16 (p.vbatMv);
            for (const auto bias : p.gyroBias)
                writer.i16 (bias);
            writer.u8 (p.fwVersion);
        }
    }, packet);

    return out;
}

std::vector<std::uint8_t> encodeFrame (const Frame& frame)
{
    const auto payload = encodePacket (frame.packet);

    std::vector<std::uint8_t> out;
    out.reserve (USB_FRAME_OVERHEAD + payload.size());
    Writer writer (out);
    writer.u8 (USB_SYNC0);
    writer.u8 (USB_SYNC1);
    writer.u8 (static_cast<std::uint8_t> (payload.size()));
    writer.u32 (frame.rxTimeUs);
    writer.i8 (frame.rssi);
    out.insert (out.end(), payload.begin(), payload.end());

    // The crc covers everything between the sync bytes and the crc itself.
    out.push_back (crc8 (out.data() + 2, out.size() - 2));
    return out;
}

std::uint8_t crc8 (const std::uint8_t* data, std::size_t size)
{
    return vdrum_crc8_update (CRC8_INIT, data, static_cast<std::uint32_t> (size));
}

float q14ToFloat (std::int16_t value)
{
    return static_cast<float> (value) / static_cast<float> (QUAT_Q14_ONE);
}

std::int16_t floatToQ14 (float value)
{
    const auto scaled = std::lround (value * static_cast<float> (QUAT_Q14_ONE));
    if (scaled < -32768)
        return -32768;
    if (scaled > 32767)
        return 32767;
    return static_cast<std::int16_t> (scaled);
}

} // namespace vdrum::protocol
