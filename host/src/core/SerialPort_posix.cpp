#include "SerialPort.h"

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/serial/IOSerialKeys.h>
#endif

namespace vdrum
{

namespace
{

std::string errnoMessage (const char* context)
{
    return std::string (context) + " failed: " + std::strerror (errno);
}

#if defined(__APPLE__)

std::string cfStringToUtf8 (CFStringRef text)
{
    if (text == nullptr)
        return {};

    char buffer[512] = {};
    if (! CFStringGetCString (text, buffer, sizeof (buffer), kCFStringEncodingUTF8))
        return {};
    return buffer;
}

// Looks up a property on the service or any of its parents (the USB device node).
CFTypeRef searchProperty (io_object_t service, CFStringRef key)
{
    return IORegistryEntrySearchCFProperty (service, kIOServicePlane, key, kCFAllocatorDefault,
                                            kIORegistryIterateRecursively | kIORegistryIterateParents);
}

std::optional<std::uint16_t> searchUsbId (io_object_t service, CFStringRef key)
{
    const auto value = searchProperty (service, key);
    if (value == nullptr)
        return std::nullopt;

    std::optional<std::uint16_t> result;
    std::int32_t number = 0;
    if (CFGetTypeID (value) == CFNumberGetTypeID()
        && CFNumberGetValue (static_cast<CFNumberRef> (value), kCFNumberSInt32Type, &number))
        result = static_cast<std::uint16_t> (number);

    CFRelease (value);
    return result;
}

#endif

} // namespace

std::vector<SerialPortInfo> listSerialPorts()
{
    std::vector<SerialPortInfo> ports;

#if defined(__APPLE__)
    auto* matching = IOServiceMatching (kIOSerialBSDServiceValue);
    if (matching == nullptr)
        return ports;
    CFDictionarySetValue (matching, CFSTR (kIOSerialBSDTypeKey), CFSTR (kIOSerialBSDAllTypes));

    io_iterator_t iterator = 0;
    // MACH_PORT_NULL selects the default main port (kIOMainPortDefault / kIOMasterPortDefault).
    if (IOServiceGetMatchingServices (MACH_PORT_NULL, matching, &iterator) != KERN_SUCCESS)
        return ports;

    while (const io_object_t service = IOIteratorNext (iterator))
    {
        const auto path = static_cast<CFStringRef> (
            IORegistryEntryCreateCFProperty (service, CFSTR (kIOCalloutDeviceKey), kCFAllocatorDefault, 0));

        if (path != nullptr)
        {
            SerialPortInfo info;
            info.path = cfStringToUtf8 (path);
            CFRelease (path);

            if (const auto product = searchProperty (service, CFSTR ("USB Product Name")))
            {
                if (CFGetTypeID (product) == CFStringGetTypeID())
                    info.description = cfStringToUtf8 (static_cast<CFStringRef> (product));
                CFRelease (product);
            }

            const auto vendorId = searchUsbId (service, CFSTR ("idVendor"));
            const auto productId = searchUsbId (service, CFSTR ("idProduct"));
            if (vendorId && productId)
            {
                info.hasUsbIds = true;
                info.vendorId = *vendorId;
                info.productId = *productId;
            }

            ports.push_back (std::move (info));
        }

        IOObjectRelease (service);
    }

    IOObjectRelease (iterator);
#endif

    return ports;
}

struct SerialPort::Impl
{
    int fd = -1;
};

SerialPort::SerialPort() : impl (std::make_unique<Impl>()) {}

SerialPort::~SerialPort()
{
    close();
}

bool SerialPort::open (const std::string& path, std::string& error)
{
    close();

    impl->fd = ::open (path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (impl->fd < 0)
    {
        error = errnoMessage ("open");
        return false;
    }

    // Exclusive access, so a second app cannot steal bytes from the stream.
    ioctl (impl->fd, TIOCEXCL);

    termios options {};
    if (tcgetattr (impl->fd, &options) != 0)
    {
        error = errnoMessage ("tcgetattr");
        close();
        return false;
    }

    cfmakeraw (&options);
    options.c_cflag |= (CLOCAL | CREAD);
    cfsetispeed (&options, B115200); // ignored by CDC ACM devices but must be valid
    cfsetospeed (&options, B115200);

    if (tcsetattr (impl->fd, TCSANOW, &options) != 0)
    {
        error = errnoMessage ("tcsetattr");
        close();
        return false;
    }

    int dtr = TIOCM_DTR;
    ioctl (impl->fd, TIOCMBIS, &dtr);
    tcflush (impl->fd, TCIOFLUSH);
    return true;
}

void SerialPort::close()
{
    if (impl->fd >= 0)
    {
        ::close (impl->fd);
        impl->fd = -1;
    }
}

bool SerialPort::isOpen() const
{
    return impl->fd >= 0;
}

long SerialPort::read (std::uint8_t* data, std::size_t size, int timeoutMs)
{
    if (! isOpen())
        return -1;

    pollfd descriptor {};
    descriptor.fd = impl->fd;
    descriptor.events = POLLIN;

    const auto ready = ::poll (&descriptor, 1, timeoutMs);
    if (ready < 0)
        return errno == EINTR ? 0 : -1;
    if (ready == 0)
        return 0;
    if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
        return -1;

    const auto bytesRead = ::read (impl->fd, data, size);
    if (bytesRead < 0)
        return (errno == EAGAIN || errno == EINTR) ? 0 : -1;
    if (bytesRead == 0)
        return -1; // readable but no data: the device went away

    return static_cast<long> (bytesRead);
}

} // namespace vdrum
