// ======================================================================
// \title  SerialStream.cpp
// \author fprime
// \brief  cpp file for the serial adaptation of the Drv::IpSocket stream interface
//
// \copyright
// Copyright 2009-2025, by the California Institute of Technology.
// ALL RIGHTS RESERVED.  United States Government Sponsorship
// acknowledged.
//
// ======================================================================

#include <Drv/UnifiedByteStreamDriver/SerialStream.hpp>
#include <Fw/Types/Assert.hpp>
#include <Fw/Types/StringUtils.hpp>

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace Drv {

namespace {

//! \brief map a baud rate to its termios speed constant
//!
//! Rates above 230400 are optional in termios, so each of those cases only exists when the
//! platform defines the matching constant. Unmapped rates fall through to the default and
//! are reported as unsupported.
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

}  // namespace

SerialStream::SerialStream()
    : IpSocket(),
      m_baud(SerialBaudRate::BAUD_115200),
      m_parity(SerialParity::PARITY_NONE),
      m_flowControl(SerialFlowControl::FLOW_NONE),
      m_stop(false) {
    (void)::memset(this->m_device, 0, sizeof(this->m_device));
}

SerialStream::~SerialStream() {}

bool SerialStream::isBaudRateSupported(const SerialBaudRate baud) {
    speed_t speed = B0;
    return baudToSpeed(baud, speed);
}

SocketIpStatus SerialStream::configureSerial(const char* const device,
                                             const SerialBaudRate baud,
                                             const SerialParity parity,
                                             const SerialFlowControl flowControl) {
    FW_ASSERT(device != nullptr);
    // Reject rather than silently truncate a device path that does not fit
    if (Fw::StringUtils::string_length(device, SERIAL_STREAM_MAX_DEVICE_SIZE) >= SERIAL_STREAM_MAX_DEVICE_SIZE) {
        return SOCK_INVALID_CALL;
    }
    if (not SerialStream::isBaudRateSupported(baud)) {
        return SOCK_INVALID_CALL;
    }
    (void)Fw::StringUtils::string_copy(this->m_device, device, SERIAL_STREAM_MAX_DEVICE_SIZE);
    this->m_baud = baud;
    this->m_parity = parity;
    this->m_flowControl = flowControl;
    return SOCK_SUCCESS;
}

SocketIpStatus SerialStream::configure(const char* const ipv4_address,
                                       const U16 port,
                                       const U32 send_timeout_seconds,
                                       const U32 send_timeout_microseconds) {
    FW_ASSERT(false);  // Must use configureSerial: a serial device has no IP endpoint
    return SOCK_INVALID_CALL;
}

void SerialStream::requestStop() {
    this->m_stop = true;
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

    switch (this->m_flowControl.e) {
        case SerialFlowControl::FLOW_NONE:
#ifdef CRTSCTS
            settings.c_cflag &= static_cast<tcflag_t>(~CRTSCTS);
#endif
            break;
        case SerialFlowControl::FLOW_HARDWARE:
#ifdef CRTSCTS
            settings.c_cflag |= static_cast<tcflag_t>(CRTSCTS);
            break;
#else
            // This platform's termios has no RTS/CTS support to turn on
            return SOCK_INVALID_CALL;
#endif
        default:
            return SOCK_INVALID_CALL;
    }

    // Raw input and output: no canonical processing, no echo, no output translation. Input
    // parity checking is only meaningful when a parity bit is actually being generated.
    settings.c_oflag = 0;
    settings.c_lflag = 0;
    settings.c_iflag = (this->m_parity == SerialParity::PARITY_NONE) ? 0 : static_cast<tcflag_t>(INPCK);

    // MIN=0 with TIME=10 makes a read with no data available return 0 after ~1 second,
    // which keeps the reader responsive to stop requests. See recvProtocol.
    settings.c_cc[VMIN] = 0;
    settings.c_cc[VTIME] = 10;

    if (::cfsetispeed(&settings, speed) != 0) {
        return SOCK_FAILED_TO_SET_SOCKET_OPTIONS;
    }
    if (::cfsetospeed(&settings, speed) != 0) {
        return SOCK_FAILED_TO_SET_SOCKET_OPTIONS;
    }

    // Drop anything the line buffered before this configuration took effect
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
    // O_NOCTTY keeps this process from adopting the device as its controlling terminal,
    // which would route terminal signals at it.
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
    // A zero-size request can never make progress, so report it as an empty read rather
    // than spinning on it below
    if (size == 0) {
        return 0;
    }
    FwSignedSizeType received = 0;
    // VTIME makes an idle line return 0 roughly once a second. Absorb those empty reads so
    // that an idle line does not push a stream of empty receives at the rest of the system,
    // while still noticing a stop request between attempts.
    // @non-terminating@: retries until data arrives, a read fails, or a stop is requested
    do {
        received = static_cast<FwSignedSizeType>(::read(socketDescriptor.fd, data, static_cast<size_t>(size)));
    } while ((received == 0) && (not this->m_stop));
    return received;
}

SocketIpStatus SerialStream::handleZeroReturn() {
    // An empty read on a serial line is a quiet line, not a closed one. It only reaches
    // here once a stop has been requested, and no-data keeps the reader from logging a
    // spurious failure on the way out.
    return SOCK_NO_DATA_AVAILABLE;
}

}  // namespace Drv
