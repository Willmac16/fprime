// ======================================================================
// \title  SerialStream.cpp
// \brief  cpp file for SerialStream, a serial device behind the IpSocket interface
// ======================================================================

#include "SerialStream.hpp"
#include <Fw/Types/Assert.hpp>
#include <Fw/Types/StringUtils.hpp>

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <cerrno>

namespace Drv {

namespace {

//! \brief map a baud rate to its termios speed
//!
//! Linux speed_t values are small indices rather than rates, so every rate needs its B
//! constant and a rate whose constant this platform does not define is unsupported. BSD and
//! macOS speed_t is the rate itself, so it needs no table and has no unsupported rate.
#ifdef __linux__
bool baudToSpeed(const SerialBaudRate baud, speed_t& speed) {
    bool supported = true;
    switch (baud.e) {
        case SerialBaudRate::BAUD_9600:
            speed = B9600;
            break;
        case SerialBaudRate::BAUD_19200:
            speed = B19200;
            break;
        case SerialBaudRate::BAUD_38400:
            speed = B38400;
            break;
        case SerialBaudRate::BAUD_57600:
            speed = B57600;
            break;
        case SerialBaudRate::BAUD_115200:
            speed = B115200;
            break;
        case SerialBaudRate::BAUD_230400:
            speed = B230400;
            break;
#ifdef B460800
        case SerialBaudRate::BAUD_460800:
            speed = B460800;
            break;
#endif
#ifdef B921600
        case SerialBaudRate::BAUD_921600:
            speed = B921600;
            break;
#endif
        default:
            supported = false;
            break;
    }
    return supported;
}
#else
bool baudToSpeed(const SerialBaudRate baud, speed_t& speed) {
    speed = static_cast<speed_t>(baud.e);
    return true;
}
#endif

}  // namespace

SerialStream::SerialStream() : IpSocket() {}

SerialStream::~SerialStream() {}

bool SerialStream::isBaudRateSupported(const SerialBaudRate baud) {
    speed_t speed = B0;
    return baudToSpeed(baud, speed);
}

bool SerialStream::isFlowControlSupported(const SerialFlowControl flowControl) {
    bool supported = false;
    switch (flowControl.e) {
        case SerialFlowControl::FLOW_NONE:
        case SerialFlowControl::FLOW_SOFTWARE:
            supported = true;
            break;
        case SerialFlowControl::FLOW_HARDWARE:
#ifdef CRTSCTS
            supported = true;
#endif
            break;
        default:
            break;
    }
    return supported;
}

SocketIpStatus SerialStream::configureSerial(const char* const device,
                                             const SerialBaudRate baud,
                                             const SerialParity parity,
                                             const SerialFlowControl flowControl,
                                             const U8 readTimeout) {
    FW_ASSERT(device != nullptr);
    // Reject rather than silently truncate a device path that does not fit
    if (Fw::StringUtils::string_length(device, SERIAL_STREAM_MAX_DEVICE_SIZE) >= SERIAL_STREAM_MAX_DEVICE_SIZE) {
        return SOCK_INVALID_CALL;
    }
    if ((not SerialStream::isBaudRateSupported(baud)) or (not SerialStream::isFlowControlSupported(flowControl))) {
        return SOCK_INVALID_CALL;
    }
    (void)Fw::StringUtils::string_copy(this->m_device, device, SERIAL_STREAM_MAX_DEVICE_SIZE);
    this->m_baud = baud;
    this->m_parity = parity;
    this->m_flowControl = flowControl;
    this->m_readTimeout = readTimeout;
    return SOCK_SUCCESS;
}

void SerialStream::requestStop() {
    this->m_stop = true;
}

void SerialStream::clearStop() {
    this->m_stop = false;
}

const char* SerialStream::getDevice() const {
    return this->m_device;
}

SocketIpStatus SerialStream::applyLineSettings(const int fd) const {
    FW_ASSERT(fd >= 0, static_cast<FwAssertArgType>(fd));
    speed_t speed = B0;
    if (not baudToSpeed(this->m_baud, speed)) {
        return SOCK_INVALID_CALL;
    }

    struct termios settings;
    if (::tcgetattr(fd, &settings) == -1) {
        return SOCK_FAILED_TO_SET_SOCKET_OPTIONS;
    }

    // 8 data bits, ignore modem control lines, enable the receiver
    settings.c_cflag &= static_cast<tcflag_t>(~CSIZE);
    settings.c_cflag |= static_cast<tcflag_t>(CS8 | CLOCAL | CREAD);

    switch (this->m_parity.e) {
        case SerialParity::PARITY_NONE:
            settings.c_cflag &= static_cast<tcflag_t>(~(PARENB | PARODD));
            break;
        case SerialParity::PARITY_EVEN:
            settings.c_cflag |= static_cast<tcflag_t>(PARENB);
            settings.c_cflag &= static_cast<tcflag_t>(~PARODD);
            break;
        case SerialParity::PARITY_ODD:
            settings.c_cflag |= static_cast<tcflag_t>(PARENB | PARODD);
            break;
        default:
            return SOCK_INVALID_CALL;
    }

#ifdef CRTSCTS
    if (this->m_flowControl == SerialFlowControl::FLOW_HARDWARE) {
        settings.c_cflag |= static_cast<tcflag_t>(CRTSCTS);
    } else {
        settings.c_cflag &= static_cast<tcflag_t>(~CRTSCTS);
    }
#else
    if (this->m_flowControl == SerialFlowControl::FLOW_HARDWARE) {
        return SOCK_INVALID_CALL;  // No RTS/CTS support on this platform to turn on
    }
#endif

    // Raw input: no break handling, no parity marking, no stripping to 7 bits, and none of
    // the carriage-return and newline translation a terminal would want. Input parity
    // checking only matters when a parity bit is generated. XON/XOFF is off unless it was
    // asked for, because leaving it on would let a 0x11 or 0x13 byte in the data stream
    // stop and start the line.
    settings.c_iflag &= static_cast<tcflag_t>(
        ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXOFF | IXANY | INPCK));
    if (this->m_parity != SerialParity::PARITY_NONE) {
        settings.c_iflag |= static_cast<tcflag_t>(INPCK);
    }
    if (this->m_flowControl == SerialFlowControl::FLOW_SOFTWARE) {
        settings.c_iflag |= static_cast<tcflag_t>(IXON | IXOFF);
    }

    // Raw output: OPOST is what would otherwise translate a newline on the way out
    settings.c_oflag &= static_cast<tcflag_t>(~OPOST);

    // Non-canonical: deliver bytes as they arrive rather than by line, do not echo them
    // back at the device, and do not turn a control character into a signal
    settings.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO | ECHOE | ECHONL | ISIG | IEXTEN));

    // MIN=0 with TIME=N makes a read with no data return 0 after N tenths of a second,
    // which keeps the reader responsive to stop requests. See recvProtocol.
    settings.c_cc[VMIN] = 0;
    settings.c_cc[VTIME] = this->m_readTimeout;

    if (::cfsetispeed(&settings, speed) != 0) {
        return SOCK_FAILED_TO_SET_SOCKET_OPTIONS;
    }
    if (::cfsetospeed(&settings, speed) != 0) {
        return SOCK_FAILED_TO_SET_SOCKET_OPTIONS;
    }
    (void)::tcflush(fd, TCIFLUSH);
    if (::tcsetattr(fd, TCSANOW, &settings) == -1) {
        return SOCK_FAILED_TO_SET_SOCKET_OPTIONS;
    }
    return SOCK_SUCCESS;
}

SocketIpStatus SerialStream::openProtocol(SocketDescriptor& socketDescriptor) {
    if (this->m_device[0] == '\0') {
        return SOCK_INVALID_CALL;  // configureSerial was never called
    }
    // O_NOCTTY keeps this process from adopting the device as its controlling terminal
    const int fd = ::open(this->m_device, O_RDWR | O_NOCTTY);
    if (fd == -1) {
        return SOCK_FAILED_TO_GET_SOCKET;
    }
    const SocketIpStatus status = this->applyLineSettings(fd);
    if (status != SOCK_SUCCESS) {
        (void)::close(fd);
        return status;
    }
    socketDescriptor.fd = fd;
    return SOCK_SUCCESS;
}

FwSignedSizeType SerialStream::sendProtocol(const SocketDescriptor& socketDescriptor,
                                            const U8* const data,
                                            const FwSizeType size) {
    FW_ASSERT(socketDescriptor.fd >= 0, static_cast<FwAssertArgType>(socketDescriptor.fd));
    FW_ASSERT((size == 0) || (data != nullptr));
    FW_ASSERT_NO_OVERFLOW(size, size_t);
    return static_cast<FwSignedSizeType>(::write(socketDescriptor.fd, data, static_cast<size_t>(size)));
}

FwSignedSizeType SerialStream::recvProtocol(const SocketDescriptor& socketDescriptor,
                                            U8* const data,
                                            const FwSizeType size) {
    FW_ASSERT(socketDescriptor.fd >= 0, static_cast<FwAssertArgType>(socketDescriptor.fd));
    FW_ASSERT(data != nullptr);
    FW_ASSERT_NO_OVERFLOW(size, size_t);
    if (size == 0) {
        return 0;  // A zero-size request cannot make progress, so do not spin on it
    }
    FwSignedSizeType received = 0;
    // VTIME makes an idle line return 0 once per read timeout. Absorb those so an idle line
    // does not push empty receives at the rest of the system, while still noticing a stop.
    // @non-terminating@: retries until data arrives, a read fails, or a stop is requested
    do {
        received = static_cast<FwSignedSizeType>(::read(socketDescriptor.fd, data, static_cast<size_t>(size)));
        if (received == 0) {
            // A device at end of file, or one whose VTIME this platform did not honor,
            // returns an immediate zero every time and would spin the read task
            (void)Os::Task::delay(SERIAL_STREAM_EMPTY_READ_DELAY);
        }
    } while ((received == 0) && (not this->m_stop));
    return received;
}

SocketIpStatus SerialStream::handleZeroReturn() {
    // Only reached once a stop was requested. No-data keeps the reader from logging a
    // spurious failure on the way out.
    return SOCK_NO_DATA_AVAILABLE;
}

}  // namespace Drv
