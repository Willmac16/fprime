// ======================================================================
// \title  UnifiedByteStreamDriver.cpp
// \author fprime
// \brief  cpp file for UnifiedByteStreamDriver component implementation class
//
// \copyright
// Copyright 2009-2025, by the California Institute of Technology.
// ALL RIGHTS RESERVED.  United States Government Sponsorship
// acknowledged.
//
// ======================================================================

#include <Drv/UnifiedByteStreamDriver/UnifiedByteStreamDriver.hpp>
#include <Fw/Logger/Logger.hpp>
#include <Fw/Types/Assert.hpp>
#include <config/IpCfg.hpp>

namespace Drv {

// ----------------------------------------------------------------------
// Construction, initialization, and destruction
// ----------------------------------------------------------------------

UnifiedByteStreamDriver::UnifiedByteStreamDriver(const char* const compName)
    : UnifiedByteStreamDriverComponentBase(compName),
      SocketComponentHelper(),
      m_mode(ByteStreamDriverMode::DISABLED),
      m_allocationSize(0),
      m_receiveEnabled(false),
      m_configured(false),
      m_started(false),
      m_bytesSent(0),
      m_bytesReceived(0) {}

UnifiedByteStreamDriver::~UnifiedByteStreamDriver() {}

// ----------------------------------------------------------------------
// Configuration
// ----------------------------------------------------------------------

bool UnifiedByteStreamDriver::isDottedQuadIpv4(const char* const address) {
    FW_ASSERT(address != nullptr);
    const char* current = address;
    U32 octets = 0;
    bool valid = true;
    // Consume one octet per iteration, stopping at the first character that is neither a
    // digit nor the '.' introducing the next octet
    while (valid) {
        U32 value = 0;
        U32 digits = 0;
        const bool leadingZero = (*current == '0');
        while ((*current >= '0') && (*current <= '9')) {
            value = (value * 10) + static_cast<U32>(*current - '0');
            digits++;
            current++;
        }
        // Each octet is one to three digits, at most 255, and only "0" itself may start
        // with a zero. inet_pton applies the same rules, so anything looser would be
        // accepted here and then rejected when the socket was opened.
        if ((digits == 0) || (digits > 3) || (value > 255) || (leadingZero && (digits > 1))) {
            valid = false;
            break;
        }
        octets++;
        if (*current != '.') {
            break;
        }
        current++;
    }
    return valid && (octets == 4) && (*current == '\0');
}

UnifiedByteStreamDriver::Parameters UnifiedByteStreamDriver::readParameters() {
    Fw::ParamValid valid = Fw::ParamValid::UNINIT;
    Parameters parameters;
    // A parameter that was never set falls back to its declared default, and every default
    // here is usable, so the validity flag is deliberately not consulted
    parameters.transport = this->paramGet_TRANSPORT(valid);
    parameters.localAddress = this->paramGet_LOCAL_ADDRESS(valid);
    parameters.localPort = this->paramGet_LOCAL_PORT(valid);
    parameters.remoteAddress = this->paramGet_REMOTE_ADDRESS(valid);
    parameters.remotePort = this->paramGet_REMOTE_PORT(valid);
    parameters.serialDevice = this->paramGet_SERIAL_DEVICE(valid);
    parameters.baudRate = this->paramGet_SERIAL_BAUD_RATE(valid);
    parameters.parity = this->paramGet_SERIAL_PARITY(valid);
    parameters.flowControl = this->paramGet_SERIAL_FLOW_CONTROL(valid);
    parameters.recvBufferSize = this->paramGet_RECV_BUFFER_SIZE(valid);
    parameters.sendTimeoutSeconds = this->paramGet_SEND_TIMEOUT_SECONDS(valid);
    parameters.sendTimeoutMicroseconds = this->paramGet_SEND_TIMEOUT_MICROSECONDS(valid);
    return parameters;
}

ByteStreamDriverMode UnifiedByteStreamDriver::reject(const ByteStreamTransport transport,
                                                     const ByteStreamConfigError error) const {
    this->log_WARNING_HI_UnsupportedConfiguration(transport, error);
    return ByteStreamDriverMode::DISABLED;
}

ByteStreamDriverMode UnifiedByteStreamDriver::configure() {
    if (this->m_configured) {
        this->log_WARNING_LO_ConfigurationChangeDeferred();
        return this->m_mode;
    }
    // The configuration is resolved once, whether or not it turns out to be usable. A
    // rejected configuration is not retried behind the operator's back: the warning it
    // emits is the whole answer.
    this->m_configured = true;
    this->m_parameters = this->readParameters();

    // Checks that apply whatever the transport is
    if (this->m_parameters.recvBufferSize == 0) {
        this->m_mode = this->reject(this->m_parameters.transport, ByteStreamConfigError::INVALID_BUFFER_SIZE);
        return this->m_mode;
    }
    if (this->m_parameters.sendTimeoutMicroseconds >= 1000000) {
        this->m_mode = this->reject(this->m_parameters.transport, ByteStreamConfigError::INVALID_SEND_TIMEOUT);
        return this->m_mode;
    }
    this->m_allocationSize = this->m_parameters.recvBufferSize;

    switch (this->m_parameters.transport.e) {
        case ByteStreamTransport::NONE:
            this->log_WARNING_HI_TransportNotConfigured();
            this->m_mode = ByteStreamDriverMode::DISABLED;
            break;
        case ByteStreamTransport::TCP:
            this->m_mode = this->configureTcp(this->m_parameters);
            break;
        case ByteStreamTransport::UDP:
            this->m_mode = this->configureUdp(this->m_parameters);
            break;
        case ByteStreamTransport::SERIAL:
            this->m_mode = this->configureSerial(this->m_parameters);
            break;
        default:
            FW_ASSERT(false, static_cast<FwAssertArgType>(this->m_parameters.transport.e));
            break;
    }

    if (this->m_mode != ByteStreamDriverMode::DISABLED) {
        this->buildEndpoint();
        this->log_ACTIVITY_HI_ConfigurationApplied(this->m_mode, this->m_endpoint);
    } else {
        // A rejected configuration must not leave a receive direction armed
        this->m_receiveEnabled = false;
    }
    return this->m_mode;
}

ByteStreamDriverMode UnifiedByteStreamDriver::configureTcp(const Parameters& parameters) {
    if (parameters.hasSerial()) {
        this->log_WARNING_LO_IgnoredConfiguration(ByteStreamConfigGroup::SERIAL, parameters.transport);
    }
    // A TCP endpoint either listens or connects, never both. Picking one for the operator
    // would silently drop half of what they asked for.
    if (parameters.hasLocal() && parameters.hasRemote()) {
        return this->reject(parameters.transport, ByteStreamConfigError::TCP_LOCAL_AND_REMOTE);
    }
    if ((not parameters.hasLocal()) && (not parameters.hasRemote())) {
        return this->reject(parameters.transport, ByteStreamConfigError::NO_ENDPOINT);
    }

    if (parameters.hasRemote()) {
        if (not UnifiedByteStreamDriver::isDottedQuadIpv4(parameters.remoteAddress.toChar())) {
            return this->reject(parameters.transport, ByteStreamConfigError::INVALID_REMOTE_ADDRESS);
        }
        // A TCP client has nothing to connect to on port 0
        if (parameters.remotePort == 0) {
            return this->reject(parameters.transport, ByteStreamConfigError::MISSING_REMOTE_PORT);
        }
        const SocketIpStatus status =
            this->m_tcpClient.configure(parameters.remoteAddress.toChar(), parameters.remotePort,
                                        parameters.sendTimeoutSeconds, parameters.sendTimeoutMicroseconds);
        FW_ASSERT(status == SOCK_SUCCESS, static_cast<FwAssertArgType>(status));
        this->m_receiveEnabled = true;
        return ByteStreamDriverMode::TCP_CLIENT;
    }

    if (not UnifiedByteStreamDriver::isDottedQuadIpv4(parameters.localAddress.toChar())) {
        return this->reject(parameters.transport, ByteStreamConfigError::INVALID_LOCAL_ADDRESS);
    }
    // A local port of 0 is legitimate here: the listener takes an ephemeral port, which
    // getLocalPort reports back once the socket is up
    const SocketIpStatus status =
        this->m_tcpServer.configure(parameters.localAddress.toChar(), parameters.localPort,
                                    parameters.sendTimeoutSeconds, parameters.sendTimeoutMicroseconds);
    FW_ASSERT(status == SOCK_SUCCESS, static_cast<FwAssertArgType>(status));
    this->m_receiveEnabled = true;
    return ByteStreamDriverMode::TCP_SERVER;
}

ByteStreamDriverMode UnifiedByteStreamDriver::configureUdp(const Parameters& parameters) {
    if (parameters.hasSerial()) {
        this->log_WARNING_LO_IgnoredConfiguration(ByteStreamConfigGroup::SERIAL, parameters.transport);
    }
    if ((not parameters.hasLocal()) && (not parameters.hasRemote())) {
        return this->reject(parameters.transport, ByteStreamConfigError::NO_ENDPOINT);
    }
    // Validate both halves before configuring either, so a rejected configuration never
    // leaves the socket half set up
    if (parameters.hasLocal() && (not UnifiedByteStreamDriver::isDottedQuadIpv4(parameters.localAddress.toChar()))) {
        return this->reject(parameters.transport, ByteStreamConfigError::INVALID_LOCAL_ADDRESS);
    }
    if (parameters.hasRemote()) {
        if (not UnifiedByteStreamDriver::isDottedQuadIpv4(parameters.remoteAddress.toChar())) {
            return this->reject(parameters.transport, ByteStreamConfigError::INVALID_REMOTE_ADDRESS);
        }
        if (parameters.remotePort == 0) {
            return this->reject(parameters.transport, ByteStreamConfigError::MISSING_REMOTE_PORT);
        }
    }

    if (parameters.hasLocal()) {
        const SocketIpStatus status = this->m_udp.configureRecv(parameters.localAddress.toChar(), parameters.localPort);
        FW_ASSERT(status == SOCK_SUCCESS, static_cast<FwAssertArgType>(status));
        this->m_receiveEnabled = true;
    }
    if (parameters.hasRemote()) {
        const SocketIpStatus status =
            this->m_udp.configureSend(parameters.remoteAddress.toChar(), parameters.remotePort,
                                      parameters.sendTimeoutSeconds, parameters.sendTimeoutMicroseconds);
        FW_ASSERT(status == SOCK_SUCCESS, static_cast<FwAssertArgType>(status));
    }
    return ByteStreamDriverMode::UDP;
}

ByteStreamDriverMode UnifiedByteStreamDriver::configureSerial(const Parameters& parameters) {
    if (parameters.hasLocal() || parameters.hasRemote()) {
        this->log_WARNING_LO_IgnoredConfiguration(ByteStreamConfigGroup::IP, parameters.transport);
    }
    if (not parameters.hasSerial()) {
        return this->reject(parameters.transport, ByteStreamConfigError::NO_SERIAL_DEVICE);
    }
    if (not SerialStream::isBaudRateSupported(parameters.baudRate)) {
        return this->reject(parameters.transport, ByteStreamConfigError::UNSUPPORTED_BAUD_RATE);
    }
    const SocketIpStatus status = this->m_serial.configureSerial(parameters.serialDevice.toChar(), parameters.baudRate,
                                                                 parameters.parity, parameters.flowControl);
    if (status != SOCK_SUCCESS) {
        // The baud rate was already checked, so the only remaining rejection is a device
        // path this build cannot hold
        return this->reject(parameters.transport, ByteStreamConfigError::NO_SERIAL_DEVICE);
    }
    this->m_receiveEnabled = true;
    return ByteStreamDriverMode::SERIAL;
}

void UnifiedByteStreamDriver::buildEndpoint() {
    // A requested port of 0 asks for an ephemeral port, whose value only exists once the
    // transport is open, so prefer the port actually in use over the requested one
    const U16 assignedPort = this->getLocalPort();
    const U16 localPort = (assignedPort != 0) ? assignedPort : this->m_parameters.localPort;
    switch (this->m_mode.e) {
        case ByteStreamDriverMode::TCP_CLIENT:
            (void)this->m_endpoint.format("%s:%hu", this->m_parameters.remoteAddress.toChar(),
                                          this->m_parameters.remotePort);
            break;
        case ByteStreamDriverMode::TCP_SERVER:
            (void)this->m_endpoint.format("%s:%hu", this->m_parameters.localAddress.toChar(), localPort);
            break;
        case ByteStreamDriverMode::UDP:
            if (this->m_parameters.hasLocal() && this->m_parameters.hasRemote()) {
                (void)this->m_endpoint.format("%s:%hu -> %s:%hu", this->m_parameters.localAddress.toChar(), localPort,
                                              this->m_parameters.remoteAddress.toChar(), this->m_parameters.remotePort);
            } else if (this->m_parameters.hasLocal()) {
                // Without a remote endpoint, UDP replies to whoever sent the last datagram
                (void)this->m_endpoint.format("%s:%hu", this->m_parameters.localAddress.toChar(), localPort);
            } else {
                (void)this->m_endpoint.format("%s:%hu", this->m_parameters.remoteAddress.toChar(),
                                              this->m_parameters.remotePort);
            }
            break;
        case ByteStreamDriverMode::SERIAL:
            (void)this->m_endpoint.format("%s @ %u", this->m_parameters.serialDevice.toChar(),
                                          static_cast<unsigned int>(this->m_parameters.baudRate.e));
            break;
        default:
            this->m_endpoint = "";
            break;
    }
}

void UnifiedByteStreamDriver::parametersLoaded() {
    (void)this->configure();
}

void UnifiedByteStreamDriver::parameterUpdated(FwPrmIdType id) {
    // The transport is configured once and then driven by a running task. Rebuilding it
    // underneath that task is not safe, so an update is acknowledged and deferred.
    if (this->m_configured) {
        this->log_WARNING_LO_ConfigurationChangeDeferred();
    }
}

// ----------------------------------------------------------------------
// Lifecycle
// ----------------------------------------------------------------------

void UnifiedByteStreamDriver::start(const FwTaskPriorityType priority,
                                    const Os::Task::ParamType stack,
                                    const Os::Task::ParamType cpuAffinity) {
    // A deployment without a parameter database never triggers parametersLoaded, so
    // resolve the configuration here when that has not happened yet
    if (not this->m_configured) {
        (void)this->configure();
    }
    if (this->m_mode == ByteStreamDriverMode::DISABLED) {
        return;  // Nothing to run: the rejection has already been reported
    }
    FW_ASSERT(not this->m_started);  // It is a coding error to start the driver twice
    this->m_started = true;
    SocketComponentHelper::start(Fw::String(this->getObjName()), priority, stack, cpuAffinity);
}

void UnifiedByteStreamDriver::stop() {
    if (not this->m_started) {
        return;
    }
    // A blocked serial read has no socket shutdown to break it out, so ask it to return
    this->m_serial.requestStop();
    SocketComponentHelper::stop();
}

Os::Task::Status UnifiedByteStreamDriver::join() {
    if (not this->m_started) {
        return Os::Task::Status::OP_OK;
    }
    return SocketComponentHelper::join();
}

ByteStreamDriverMode UnifiedByteStreamDriver::getMode() const {
    return this->m_mode;
}

U16 UnifiedByteStreamDriver::getLocalPort() {
    U16 port = 0;
    switch (this->m_mode.e) {
        case ByteStreamDriverMode::TCP_SERVER:
            port = this->m_tcpServer.getListenPort();
            break;
        case ByteStreamDriverMode::UDP:
            port = this->m_receiveEnabled ? this->m_udp.getRecvPort() : static_cast<U16>(0);
            break;
        default:
            port = 0;  // A TCP client and a serial line have no local port to report
            break;
    }
    return port;
}

// ----------------------------------------------------------------------
// Implementations for socket read task virtual methods
// ----------------------------------------------------------------------

IpSocket& UnifiedByteStreamDriver::getSocketHandler() {
    switch (this->m_mode.e) {
        case ByteStreamDriverMode::TCP_CLIENT:
            return this->m_tcpClient;
        case ByteStreamDriverMode::TCP_SERVER:
            return this->m_tcpServer;
        case ByteStreamDriverMode::UDP:
            return this->m_udp;
        case ByteStreamDriverMode::SERIAL:
            return this->m_serial;
        default:
            // Every path that reaches a transport is gated on a resolved mode, so a
            // disabled driver arriving here is a coding error
            FW_ASSERT(false, static_cast<FwAssertArgType>(this->m_mode.e));
            break;
    }
    return this->m_udp;  // Unreachable: the assert above does not return
}

Fw::Buffer UnifiedByteStreamDriver::getBuffer() {
    Fw::Buffer buffer = this->allocate_out(0, this->m_allocationSize);
    if (not buffer.isValid()) {
        this->log_WARNING_HI_NoBuffers();
    }
    return buffer;
}

void UnifiedByteStreamDriver::sendBuffer(Fw::Buffer buffer, SocketIpStatus status) {
    // A successful receive must have produced a buffer with backing data (size may be zero)
    FW_ASSERT((status != SOCK_SUCCESS) || (buffer.getData() != nullptr));
    Drv::ByteStreamStatus recvStatus = ByteStreamStatus::OTHER_ERROR;
    if (status == SOCK_SUCCESS) {
        recvStatus = ByteStreamStatus::OP_OK;
        this->m_bytesReceived += buffer.getSize();
    } else if (status == SOCK_NO_DATA_AVAILABLE) {
        recvStatus = ByteStreamStatus::RECV_NO_DATA;
    } else {
        recvStatus = ByteStreamStatus::OTHER_ERROR;
        this->log_WARNING_LO_ReceiveError(static_cast<I32>(status));
    }
    this->recv_out(0, buffer, recvStatus);
}

void UnifiedByteStreamDriver::connected() {
    // The endpoint description is rebuilt here so that an ephemeral port shows up as the
    // port actually assigned rather than as 0
    this->buildEndpoint();
    this->log_ACTIVITY_HI_PortOpened(this->m_mode, this->m_endpoint);
    if (this->isConnected_ready_OutputPort(0)) {
        this->ready_out(0);
    }
}

SocketIpStatus UnifiedByteStreamDriver::startupServer() {
    Os::ScopeLock scopedLock(this->m_lock);
    SocketIpStatus status = SOCK_SUCCESS;
    // Prevent multiple startup attempts
    if (this->m_descriptor.serverFd == -1) {
        status = this->m_tcpServer.startup(this->m_descriptor);
    }
    return status;
}

void UnifiedByteStreamDriver::terminateServer() {
    SocketComponentHelper::stop();
    Os::ScopeLock scopedLock(this->m_lock);
    this->m_tcpServer.terminate(this->m_descriptor);
    this->m_descriptor.serverFd = -1;
}

void UnifiedByteStreamDriver::holdOpenLoop() {
    // A send-only transport has no receive direction to block on, but the connection still
    // has to be opened, and reopened, for the send path to use
    // @non-terminating@: runs until the driver is stopped
    while (this->running()) {
        if (not this->isOpened()) {
            this->requestReconnect();
            if (this->waitForReconnect() == SOCK_AUTO_CONNECT_DISABLED) {
                break;
            }
        }
        (void)Os::Task::delay(SOCKET_RETRY_INTERVAL);
    }
    this->close();
}

void UnifiedByteStreamDriver::readLoop() {
    if (this->m_mode == ByteStreamDriverMode::TCP_SERVER) {
        Drv::SocketIpStatus status = Drv::SocketIpStatus::SOCK_NOT_STARTED;
        // Keep trying to listen until it works, a stop is requested, or reopen is disabled
        // @non-terminating@: retry loop bounded by stop request
        do {
            status = this->startupServer();
            if (status != SOCK_SUCCESS) {
                Fw::Logger::log("[WARNING] Failed to listen on port %hu with status %d\n",
                                this->m_tcpServer.getListenPort(), status);
                (void)Os::Task::delay(SOCKET_RETRY_INTERVAL);
            }
        } while (this->running() && (status != SOCK_SUCCESS) && this->getAutomaticOpen());

        if (this->running() && (status == SOCK_SUCCESS)) {
            SocketComponentHelper::readLoop();
        }
        this->terminateServer();
    } else if (not this->m_receiveEnabled) {
        this->holdOpenLoop();
    } else {
        SocketComponentHelper::readLoop();
    }
}

// ----------------------------------------------------------------------
// Handler implementations for user-defined typed input ports
// ----------------------------------------------------------------------

Drv::ByteStreamStatus UnifiedByteStreamDriver::send_handler(const FwIndexType portNum, Fw::Buffer& fwBuffer) {
    if (this->m_mode == ByteStreamDriverMode::DISABLED) {
        return ByteStreamStatus::OTHER_ERROR;
    }
    const FwSizeType size = fwBuffer.getSize();
    const Drv::SocketIpStatus status = this->send(fwBuffer.getData(), size);
    Drv::ByteStreamStatus returnStatus = ByteStreamStatus::OTHER_ERROR;
    switch (status) {
        case SOCK_SUCCESS:
            this->m_bytesSent += size;
            returnStatus = ByteStreamStatus::OP_OK;
            break;
        // The read task owns reopening the transport, so the caller retries rather than
        // reopening it itself
        case SOCK_INTERRUPTED_TRY_AGAIN:
        case SOCK_DISCONNECTED:
            returnStatus = ByteStreamStatus::SEND_RETRY;
            break;
        default:
            returnStatus = ByteStreamStatus::OTHER_ERROR;
            break;
    }
    if (returnStatus != ByteStreamStatus::OP_OK) {
        this->log_WARNING_LO_SendError(static_cast<I32>(status));
    }
    return returnStatus;
}

void UnifiedByteStreamDriver::recvReturnIn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) {
    this->deallocate_out(0, fwBuffer);
}

void UnifiedByteStreamDriver::run_handler(FwIndexType portNum, U32 context) {
    this->tlmWrite_BytesSent(this->m_bytesSent);
    this->tlmWrite_BytesReceived(this->m_bytesReceived);
    this->tlmWrite_Mode(this->m_mode);
    this->tlmWrite_Connected(this->isOpened());
}

}  // end namespace Drv
