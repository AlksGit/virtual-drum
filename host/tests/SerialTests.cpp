#include "TestFramework.h"

#include "SerialDataSource.h"
#include "SerialPort.h"

namespace
{

class NullListener final : public vdrum::DataSource::Listener
{
public:
    void frameReceived (const vdrum::protocol::Frame&) override {}
    void sourceFailed (const std::string&) override {}
};

#if defined(_WIN32)
const char* const kMissingPort = "COM250";
#else
const char* const kMissingPort = "/dev/vdrum-missing-port";
#endif

} // namespace

VDRUM_TEST (listingSerialPortsSucceeds)
{
    // Hardware-independent: only checks that enumeration works and fills paths.
    for (const auto& port : vdrum::listSerialPorts())
        CHECK (! port.path.empty());
}

VDRUM_TEST (openingAMissingPortFailsWithAMessage)
{
    vdrum::SerialPort port;
    std::string error;
    CHECK (! port.open (kMissingPort, error));
    CHECK (! error.empty());
    CHECK (! port.isOpen());
    CHECK (port.read (nullptr, 0, 0) == -1);
}

VDRUM_TEST (serialSourceReportsAMissingPort)
{
    vdrum::SerialDataSource source (kMissingPort);
    NullListener listener;
    std::string error;
    CHECK (! source.start (listener, error));
    CHECK (! error.empty());
    CHECK (! source.isRunning());
    source.stop(); // must be safe when not running
}
