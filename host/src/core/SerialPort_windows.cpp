#include "SerialPort.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <setupapi.h>
#include <initguid.h> // must precede devguid.h so that GUID_DEVCLASS_PORTS is defined
#include <devguid.h>

#include <cwctype>

namespace vdrum
{

namespace
{

std::string toUtf8 (const std::wstring& text)
{
    if (text.empty())
        return {};

    const auto size = WideCharToMultiByte (CP_UTF8, 0, text.data(), static_cast<int> (text.size()), nullptr, 0, nullptr, nullptr);
    std::string out (static_cast<std::size_t> (size), '\0');
    WideCharToMultiByte (CP_UTF8, 0, text.data(), static_cast<int> (text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring fromUtf8 (const std::string& text)
{
    if (text.empty())
        return {};

    const auto size = MultiByteToWideChar (CP_UTF8, 0, text.data(), static_cast<int> (text.size()), nullptr, 0);
    std::wstring out (static_cast<std::size_t> (size), L'\0');
    MultiByteToWideChar (CP_UTF8, 0, text.data(), static_cast<int> (text.size()), out.data(), size);
    return out;
}

std::string lastErrorMessage (const char* context)
{
    return std::string (context) + " failed (Windows error " + std::to_string (GetLastError()) + ")";
}

std::wstring readStringProperty (HDEVINFO devices, SP_DEVINFO_DATA& device, DWORD property)
{
    wchar_t buffer[512] = {};
    if (! SetupDiGetDeviceRegistryPropertyW (devices, &device, property, nullptr,
                                             reinterpret_cast<PBYTE> (buffer), sizeof (buffer) - sizeof (wchar_t), nullptr))
        return {};

    return buffer; // for REG_MULTI_SZ this keeps the first string
}

std::wstring readPortName (HDEVINFO devices, SP_DEVINFO_DATA& device)
{
    const auto key = SetupDiOpenDevRegKey (devices, &device, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
    if (key == INVALID_HANDLE_VALUE)
        return {};

    wchar_t buffer[64] = {};
    DWORD size = sizeof (buffer) - sizeof (wchar_t);
    DWORD type = 0;
    const auto status = RegQueryValueExW (key, L"PortName", nullptr, &type, reinterpret_cast<LPBYTE> (buffer), &size);
    RegCloseKey (key);

    return (status == ERROR_SUCCESS && type == REG_SZ) ? std::wstring (buffer) : std::wstring();
}

// Parses "VID_xxxx" / "PID_xxxx" from a hardware id such as "USB\VID_2FE3&PID_0100&MI_00".
std::optional<std::uint16_t> parseHexId (const std::wstring& hardwareId, const wchar_t* tag)
{
    const auto pos = hardwareId.find (tag);
    if (pos == std::wstring::npos || pos + 8 > hardwareId.size())
        return std::nullopt;

    std::uint16_t value = 0;
    for (std::size_t i = pos + 4; i < pos + 8; ++i)
    {
        const auto c = std::towupper (hardwareId[i]);
        int digit = 0;
        if (c >= L'0' && c <= L'9')
            digit = static_cast<int> (c - L'0');
        else if (c >= L'A' && c <= L'F')
            digit = static_cast<int> (c - L'A' + 10);
        else
            return std::nullopt;
        value = static_cast<std::uint16_t> ((value << 4) | digit);
    }
    return value;
}

} // namespace

std::vector<SerialPortInfo> listSerialPorts()
{
    std::vector<SerialPortInfo> ports;

    const auto devices = SetupDiGetClassDevsW (&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
    if (devices == INVALID_HANDLE_VALUE)
        return ports;

    SP_DEVINFO_DATA device {};
    device.cbSize = sizeof (device);

    for (DWORD index = 0; SetupDiEnumDeviceInfo (devices, index, &device); ++index)
    {
        const auto portName = readPortName (devices, device);
        if (portName.rfind (L"COM", 0) != 0)
            continue; // skip LPT and other non-COM entries of the Ports class

        SerialPortInfo info;
        info.path = toUtf8 (portName);
        info.description = toUtf8 (readStringProperty (devices, device, SPDRP_FRIENDLYNAME));

        const auto hardwareId = readStringProperty (devices, device, SPDRP_HARDWAREID);
        const auto vendorId = parseHexId (hardwareId, L"VID_");
        const auto productId = parseHexId (hardwareId, L"PID_");
        if (vendorId && productId)
        {
            info.hasUsbIds = true;
            info.vendorId = *vendorId;
            info.productId = *productId;
        }

        ports.push_back (std::move (info));
    }

    SetupDiDestroyDeviceInfoList (devices);
    return ports;
}

struct SerialPort::Impl
{
    HANDLE handle = INVALID_HANDLE_VALUE;
    int currentTimeoutMs = -1;
};

SerialPort::SerialPort() : impl (std::make_unique<Impl>()) {}

SerialPort::~SerialPort()
{
    close();
}

bool SerialPort::open (const std::string& path, std::string& error)
{
    close();

    // The \\.\ prefix is required for COM10 and above, and harmless below.
    const auto devicePath = L"\\\\.\\" + fromUtf8 (path);
    impl->handle = CreateFileW (devicePath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (impl->handle == INVALID_HANDLE_VALUE)
    {
        error = lastErrorMessage ("CreateFile");
        return false;
    }

    DCB dcb {};
    dcb.DCBlength = sizeof (dcb);
    if (! GetCommState (impl->handle, &dcb))
    {
        error = lastErrorMessage ("GetCommState");
        close();
        return false;
    }

    // The baud rate is ignored by CDC ACM devices but must be valid.
    dcb.BaudRate = CBR_115200;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fTXContinueOnXoff = TRUE;
    // GetCommState returns whatever the previous user of the port left: reset the fields
    // that would corrupt a binary stream (fNull strips 0x00 bytes) or stall reads.
    dcb.fNull = FALSE;
    dcb.fErrorChar = FALSE;
    dcb.fAbortOnError = FALSE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;

    if (! SetCommState (impl->handle, &dcb))
    {
        error = lastErrorMessage ("SetCommState");
        close();
        return false;
    }

    PurgeComm (impl->handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
    impl->currentTimeoutMs = -1;
    return true;
}

void SerialPort::close()
{
    if (impl->handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle (impl->handle);
        impl->handle = INVALID_HANDLE_VALUE;
    }
}

bool SerialPort::isOpen() const
{
    return impl->handle != INVALID_HANDLE_VALUE;
}

long SerialPort::read (std::uint8_t* data, std::size_t size, int timeoutMs)
{
    if (! isOpen())
        return -1;

    if (timeoutMs != impl->currentTimeoutMs)
    {
        // Returns as soon as at least one byte is available, or after timeoutMs.
        COMMTIMEOUTS timeouts {};
        timeouts.ReadIntervalTimeout = MAXDWORD;
        timeouts.ReadTotalTimeoutMultiplier = MAXDWORD;
        timeouts.ReadTotalTimeoutConstant = static_cast<DWORD> (timeoutMs > 0 ? timeoutMs : 1);
        if (! SetCommTimeouts (impl->handle, &timeouts))
            return -1;
        impl->currentTimeoutMs = timeoutMs;
    }

    DWORD bytesRead = 0;
    const auto toRead = static_cast<DWORD> (size > MAXDWORD ? MAXDWORD : size);
    if (! ReadFile (impl->handle, data, toRead, &bytesRead, nullptr))
    {
        // A line error can make ReadFile fail until it is cleared: clear it and retry once.
        // If the retry fails too, the device is most likely gone.
        DWORD errors = 0;
        if (! ClearCommError (impl->handle, &errors, nullptr)
            || ! ReadFile (impl->handle, data, toRead, &bytesRead, nullptr))
            return -1;
    }

    return static_cast<long> (bytesRead);
}

} // namespace vdrum
