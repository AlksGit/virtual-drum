#include "SerialPort.h"

extern "C" {
#include "protocol.h"
}

namespace vdrum
{

std::optional<SerialPortInfo> findReceiverPort()
{
    for (const auto& port : listSerialPorts())
        if (port.hasUsbIds && port.vendorId == USB_RECEIVER_VID && port.productId == USB_RECEIVER_PID)
            return port;

    return std::nullopt;
}

} // namespace vdrum
