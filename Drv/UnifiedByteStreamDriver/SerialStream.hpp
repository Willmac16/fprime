// ======================================================================
// \title  SerialStream.hpp
// \brief  hpp file for SerialStream, a serial device behind the IpSocket interface
// ======================================================================
#ifndef DRV_UNIFIEDBYTESTREAMDRIVER_SERIALSTREAM_HPP
#define DRV_UNIFIEDBYTESTREAMDRIVER_SERIALSTREAM_HPP

#include <Drv/Ip/IpSocket.hpp>
#include <Drv/UnifiedByteStreamDriver/SerialBaudRateEnumAc.hpp>
#include <Drv/UnifiedByteStreamDriver/SerialFlowControlEnumAc.hpp>
#include <Drv/UnifiedByteStreamDriver/SerialParityEnumAc.hpp>
#include <Fw/FPrimeBasicTypes.hpp>
#include <Os/Task.hpp>
#include <atomic>

namespace Drv {

//! Maximum length of a serial device path, NUL included
static const FwSizeType SERIAL_STREAM_MAX_DEVICE_SIZE = 128;

//! Wait between empty reads, so a device that returns an immediate zero cannot spin the
//! read task. Well under the read timeout an idle line already spends inside a single read.
static const Fw::TimeInterval SERIAL_STREAM_EMPTY_READ_DELAY = Fw::TimeInterval(0, 10000);

/**
 * \brief a POSIX (termios) serial device behind the Drv::IpSocket interface
 *
 * Drv::IpSocket is really a byte-stream endpoint interface: openProtocol, sendProtocol,
 * recvProtocol, close and shutdown. Implementing it for a serial device lets
 * Drv::SocketComponentHelper drive TCP, UDP and serial with one read loop. The cost is
 * that Drv::SocketIpStatus carries non-socket errors here:
 *
 * | condition                  | reported status                   |
 * |----------------------------|-----------------------------------|
 * | `::open` of device failed  | SOCK_FAILED_TO_GET_SOCKET         |
 * | a termios call failed      | SOCK_FAILED_TO_SET_SOCKET_OPTIONS |
 * | baud rate unsupported      | SOCK_INVALID_CALL                 |
 */
class SerialStream final : public IpSocket {
  public:
    SerialStream();
    ~SerialStream() override;

    //! \brief configure the device without opening it
    //! \param readTimeout: tenths of a second a read waits on an idle line (termios VTIME)
    //! \return SOCK_INVALID_CALL when a setting is unsupported or the path does not fit
    SocketIpStatus configureSerial(const char* const device,
                                   const SerialBaudRate baud,
                                   const SerialParity parity,
                                   const SerialFlowControl flowControl,
                                   const U8 readTimeout);

    //! \brief make a blocked receive return so the read task can exit
    void requestStop();

    //! \brief clear a previous stop request, so a stopped stream can be reopened
    void clearStop();

    //! \brief whether this platform's termios defines the given baud rate
    static bool isBaudRateSupported(const SerialBaudRate baud);

    //! \brief whether this platform's termios can serve the given flow control mode
    static bool isFlowControlSupported(const SerialFlowControl flowControl);

    //! \brief configured device path, empty until configureSerial succeeds
    const char* getDevice() const;

  protected:
    SocketIpStatus openProtocol(SocketDescriptor& socketDescriptor) override;

    FwSignedSizeType sendProtocol(const SocketDescriptor& socketDescriptor,
                                  const U8* const data,
                                  const FwSizeType size) override;

    //! \brief read, absorbing the empty reads an idle line produces
    FwSignedSizeType recvProtocol(const SocketDescriptor& socketDescriptor,
                                  U8* const data,
                                  const FwSizeType size) override;

    //! \brief an empty read means a quiet line, not a closed one
    SocketIpStatus handleZeroReturn() override;

  private:
    SocketIpStatus applyLineSettings(const int fd) const;

    char m_device[SERIAL_STREAM_MAX_DEVICE_SIZE] = {};
    SerialBaudRate m_baud = SerialBaudRate::BAUD_115200;
    SerialParity m_parity = SerialParity::PARITY_NONE;
    SerialFlowControl m_flowControl = SerialFlowControl::FLOW_NONE;
    U8 m_readTimeout = 10;
    std::atomic<bool> m_stop{false};  //!< set to break out of a blocked receive
};

}  // namespace Drv

#endif  // DRV_UNIFIEDBYTESTREAMDRIVER_SERIALSTREAM_HPP
