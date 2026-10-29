#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace vdrum
{

struct SerialPortInfo
{
    std::string path;        // "COM5" on Windows, "/dev/cu.usbmodem1101" on macOS
    std::string description; // human-readable name, may be empty
    bool hasUsbIds = false;
    std::uint16_t vendorId = 0;
    std::uint16_t productId = 0;
};

// Lists the serial ports currently present. USB ids are filled when the OS exposes them.
std::vector<SerialPortInfo> listSerialPorts();

// Returns the first port whose USB ids match the Virtual Drum receiver (see protocol.h).
std::optional<SerialPortInfo> findReceiverPort();

// Minimal blocking serial port, enough for a USB CDC ACM device.
// Not thread-safe: use one port from one thread.
class SerialPort
{
public:
    SerialPort();
    ~SerialPort();

    SerialPort (const SerialPort&) = delete;
    SerialPort& operator= (const SerialPort&) = delete;

    // Opens the port in raw 8N1 mode with DTR asserted (CDC ACM devices often wait for it).
    bool open (const std::string& path, std::string& error);
    void close();
    bool isOpen() const;

    // Reads up to `size` bytes, waiting at most `timeoutMs` for the first byte.
    // Returns the number of bytes read (0 on timeout), or -1 on error / disconnection.
    long read (std::uint8_t* data, std::size_t size, int timeoutMs);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace vdrum
