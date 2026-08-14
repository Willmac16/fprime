// ======================================================================
// \title  SerialStream.hpp
// \author fprime
// \brief  hpp file for the serial adaptation of the Drv::IpSocket stream interface
//
// \copyright
// Copyright 2009-2025, by the California Institute of Technology.
// ALL RIGHTS RESERVED.  United States Government Sponsorship
// acknowledged.
//
// ======================================================================
#ifndef DRV_UNIFIEDBYTESTREAMDRIVER_SERIALSTREAM_HPP
#define DRV_UNIFIEDBYTESTREAMDRIVER_SERIALSTREAM_HPP

#include <Drv/Ip/IpSocket.hpp>
#include <Drv/UnifiedByteStreamDriver/SerialBaudRateEnumAc.hpp>
#include <Drv/UnifiedByteStreamDriver/SerialFlowControlEnumAc.hpp>
#include <Drv/UnifiedByteStreamDriver/SerialParityEnumAc.hpp>
#include <Fw/FPrimeBasicTypes.hpp>
#include <atomic>

namespace Drv {

//! Maximum length of a serial device path handled by this class, NUL included
static const FwSizeType SERIAL_STREAM_MAX_DEVICE_SIZE = 128;

/**
 * \brief adapts a POSIX (termios) serial device to the Drv::IpSocket stream interface
 *
 * Drv::SocketComponentHelper drives a read task, a reconnect task and the open/close
 * lifecycle of anything that presents the Drv::IpSocket interface. That interface is
 * really a byte-stream endpoint interface: `openProtocol`, `sendProtocol`, `recvProtocol`
 * plus close/shutdown. Implementing it for a serial device lets a single component drive
 * TCP, UDP and serial through exactly the same machinery, at the cost of reusing the
 * Drv::SocketIpStatus enumeration for non-socket errors:
 *
 * | condition                    | reported status                     |
 * |------------------------------|-------------------------------------|
 * | `::open` of the device failed | SOCK_FAILED_TO_GET_SOCKET          |
 * | a termios call failed         | SOCK_FAILED_TO_SET_SOCKET_OPTIONS  |
 * | the baud rate is unsupported  | SOCK_INVALID_CALL                  |
 *
 * The device is opened in non-canonical mode with 8 data bits and VMIN=0/VTIME=10, so a
 * read that finds no data returns after roughly one second. `recvProtocol` absorbs those
 * empty reads and keeps reading so that idle lines do not produce a stream of empty
 * receives; it returns only once data has arrived or `requestStop` has been called.
 */
class SerialStream final : public IpSocket {
  public:
    //! \brief construct an unconfigured serial stream
    SerialStream();

    //! \brief destroy the serial stream
    ~SerialStream() override;

    /**
     * \brief configure the serial device, but do not open it
     *
     * \param device: NUL-terminated device path, e.g. "/dev/ttyUSB0"
     * \param baud: baud rate of the line
     * \param parity: parity of the line
     * \param flowControl: flow control of the line
     * \return SOCK_SUCCESS on success, SOCK_INVALID_CALL when the baud rate is not
     *         supported by this platform or the device path does not fit
     */
    SocketIpStatus configureSerial(const char* const device,
                                   const SerialBaudRate baud,
                                   const SerialParity parity,
                                   const SerialFlowControl flowControl);

    /**
     * \brief IP configuration is not valid for a serial device
     *
     * \warning it is a coding error to call this method. Use `configureSerial`.
     */
    SocketIpStatus configure(const char* const ipv4_address,
                             const U16 port,
                             const U32 send_timeout_seconds,
                             const U32 send_timeout_microseconds) override;

    /**
     * \brief ask a blocked receive to return
     *
     * `recvProtocol` blocks until data arrives. Calling this makes the next timed-out read
     * return instead of looping, which lets the driving read task exit.
     */
    void requestStop();

    /**
     * \brief check whether this platform supports the given baud rate
     *
     * Baud rates above 230400 are optional in termios and are not defined by every
     * platform. This reports whether the rate can be requested on this build.
     *
     * \param baud: baud rate to check
     * \return true when the rate is supported, false otherwise
     */
    static bool isBaudRateSupported(const SerialBaudRate baud);

    /**
     * \brief get the configured device path
     *
     * \return NUL-terminated device path, empty when not yet configured
     */
    const char* getDevice() const;

  protected:
    //! \brief open and configure the serial device
    SocketIpStatus openProtocol(SocketDescriptor& socketDescriptor) override;

    //! \brief write to the serial device
    FwSignedSizeType sendProtocol(const SocketDescriptor& socketDescriptor,
                                  const U8* const data,
                                  const FwSizeType size) override;

    //! \brief read from the serial device, absorbing empty reads
    FwSignedSizeType recvProtocol(const SocketDescriptor& socketDescriptor,
                                  U8* const data,
                                  const FwSizeType size) override;

    //! \brief an empty read on a serial line means "no data yet", not a disconnect
    SocketIpStatus handleZeroReturn() override;

  private:
    //! \brief apply the configured line settings to an open file descriptor
    SocketIpStatus applyLineSettings(const int fd) const;

    char m_device[SERIAL_STREAM_MAX_DEVICE_SIZE] = {};               //!< device path
    SerialBaudRate m_baud = SerialBaudRate::BAUD_115200;             //!< baud rate of the line
    SerialParity m_parity = SerialParity::PARITY_NONE;               //!< parity of the line
    SerialFlowControl m_flowControl = SerialFlowControl::FLOW_NONE;  //!< flow control of the line
    std::atomic<bool> m_stop{false};                                 //!< set to break out of a blocked receive
};

}  // namespace Drv

#endif  // DRV_UNIFIEDBYTESTREAMDRIVER_SERIALSTREAM_HPP
