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

#include "UnifiedByteStreamDriver.hpp"
#include <Fw/Logger/Logger.hpp>
#include <Fw/Types/Assert.hpp>
#include <config/IpCfg.hpp>

namespace Drv {

// ----------------------------------------------------------------------
// Construction, initialization, and destruction
// ----------------------------------------------------------------------

UnifiedByteStreamDriver::UnifiedByteStreamDriver(const char* const compName)
    : UnifiedByteStreamDriverComponentBase(compName), SocketComponentHelper() {}

UnifiedByteStreamDriver::~UnifiedByteStreamDriver() {}

// ----------------------------------------------------------------------
// Configuration
// ----------------------------------------------------------------------

void UnifiedByteStreamDriver::formatAddress(const IpEndpoint& endpoint, Fw::String& address) {
    // The socket layer takes a dotted-quad string, and typed octets always produce a valid
    // one, which is why this driver has no address validation to do
    const IpEndpoint::Type_of_address& octets = endpoint.get_address();
    (void)address.format("%u.%u.%u.%u", static_cast<unsigned int>(octets[0]), static_cast<unsigned int>(octets[1]),
                         static_cast<unsigned int>(octets[2]), static_cast<unsigned int>(octets[3]));
}

UnifiedByteStreamDriver::Parameters UnifiedByteStreamDriver::readParameters() {
    Fw::ParamValid valid = Fw::ParamValid::UNINIT;
    Parameters parameters;
    // A parameter that was never set falls back to its declared default, and every default
    // here is usable, so the validity flag is deliberately not consulted
    parameters.transport = this->paramGet_TRANSPORT(valid);
    parameters.localEndpoint = this->paramGet_LOCAL_ENDPOINT(valid);
    parameters.remoteEndpoint = this->paramGet_REMOTE_ENDPOINT(valid);
    parameters.serialDevice = this->paramGet_SERIAL_DEVICE(valid);
    parameters.baudRate = this->paramGet_SERIAL_BAUD_RATE(valid);
    parameters.parity = this->paramGet_SERIAL_PARITY(valid);
    parameters.flowControl = this->paramGet_SERIAL_FLOW_CONTROL(valid);
    parameters.recvBufferSize = this->paramGet_RECV_BUFFER_SIZE(valid);
    parameters.sendTimeoutSeconds = this->paramGet_SEND_TIMEOUT_SECONDS(valid);
    parameters.sendTimeoutMicroseconds = this->paramGet_SEND_TIMEOUT_MICROSECONDS(valid);
    return parameters;
}

ByteStreamTransport UnifiedByteStreamDriver::reject(const ByteStreamTransport transport,
                                                    const ByteStreamConfigError error) const {
    this->log_WARNING_HI_UnsupportedConfiguration(transport, error);
    return ByteStreamTransport::NONE;
}

ByteStreamTransport UnifiedByteStreamDriver::configure() {
    if (this->m_configured) {
        this->log_WARNING_LO_ConfigurationChangeDeferred();
        return this->m_transport;
    }
    // The configuration is resolved once, whether or not it turns out to be usable. A
    // rejected configuration is not retried behind the operator's back: the warning it
    // emits is the whole answer.
    this->m_configured = true;
    const Parameters parameters = this->readParameters();

    // Checks that apply whatever the transport is
    if (parameters.recvBufferSize == 0) {
        this->m_transport = this->reject(parameters.transport, ByteStreamConfigError::INVALID_BUFFER_SIZE);
        return this->m_transport;
    }
    if (parameters.sendTimeoutMicroseconds >= 1000000) {
        this->m_transport = this->reject(parameters.transport, ByteStreamConfigError::INVALID_SEND_TIMEOUT);
        return this->m_transport;
    }
    this->m_allocationSize = parameters.recvBufferSize;

    switch (parameters.transport.e) {
        case ByteStreamTransport::NONE:
            this->log_WARNING_HI_TransportNotConfigured();
            this->m_transport = ByteStreamTransport::NONE;
            break;
        case ByteStreamTransport::TCP:
            this->m_transport = this->configureTcp(parameters);
            break;
        case ByteStreamTransport::UDP:
            this->m_transport = this->configureUdp(parameters);
            break;
        case ByteStreamTransport::SERIAL:
            this->m_transport = this->configureSerial(parameters);
            break;
        default:
            FW_ASSERT(false, static_cast<FwAssertArgType>(parameters.transport.e));
            break;
    }

    if (this->m_transport != ByteStreamTransport::NONE) {
        this->buildEndpoint(parameters);
        this->log_ACTIVITY_HI_ConfigurationApplied(this->m_transport, this->m_endpoint);
    } else {
        // A rejected configuration must not leave a receive direction armed
        this->m_receiveEnabled = false;
    }
    return this->m_transport;
}

ByteStreamTransport UnifiedByteStreamDriver::configureTcp(const Parameters& parameters) {
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

    Fw::String address;
    this->m_listening = parameters.hasLocal();
    const IpEndpoint& endpoint = this->m_listening ? parameters.localEndpoint : parameters.remoteEndpoint;
    UnifiedByteStreamDriver::formatAddress(endpoint, address);

    IpSocket& socket =
        this->m_listening ? static_cast<IpSocket&>(this->m_tcpServer) : static_cast<IpSocket&>(this->m_tcpClient);
    const SocketIpStatus status = socket.configure(address.toChar(), endpoint.get_port(), parameters.sendTimeoutSeconds,
                                                   parameters.sendTimeoutMicroseconds);
    FW_ASSERT(status == SOCK_SUCCESS, static_cast<FwAssertArgType>(status));
    this->m_receiveEnabled = true;
    return ByteStreamTransport::TCP;
}

ByteStreamTransport UnifiedByteStreamDriver::configureUdp(const Parameters& parameters) {
    if (parameters.hasSerial()) {
        this->log_WARNING_LO_IgnoredConfiguration(ByteStreamConfigGroup::SERIAL, parameters.transport);
    }
    if ((not parameters.hasLocal()) && (not parameters.hasRemote())) {
        return this->reject(parameters.transport, ByteStreamConfigError::NO_ENDPOINT);
    }

    Fw::String address;
    if (parameters.hasLocal()) {
        UnifiedByteStreamDriver::formatAddress(parameters.localEndpoint, address);
        const SocketIpStatus status = this->m_udp.configureRecv(address.toChar(), parameters.localEndpoint.get_port());
        FW_ASSERT(status == SOCK_SUCCESS, static_cast<FwAssertArgType>(status));
        this->m_receiveEnabled = true;
    }
    if (parameters.hasRemote()) {
        UnifiedByteStreamDriver::formatAddress(parameters.remoteEndpoint, address);
        const SocketIpStatus status =
            this->m_udp.configureSend(address.toChar(), parameters.remoteEndpoint.get_port(),
                                      parameters.sendTimeoutSeconds, parameters.sendTimeoutMicroseconds);
        FW_ASSERT(status == SOCK_SUCCESS, static_cast<FwAssertArgType>(status));
    }
    return ByteStreamTransport::UDP;
}

ByteStreamTransport UnifiedByteStreamDriver::configureSerial(const Parameters& parameters) {
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
    return ByteStreamTransport::SERIAL;
}

void UnifiedByteStreamDriver::buildEndpoint(const Parameters& parameters) {
    Fw::String local;
    Fw::String remote;
    UnifiedByteStreamDriver::formatAddress(parameters.localEndpoint, local);
    UnifiedByteStreamDriver::formatAddress(parameters.remoteEndpoint, remote);
    switch (this->m_transport.e) {
        case ByteStreamTransport::TCP:
            if (this->m_listening) {
                (void)this->m_endpoint.format("listen %s:%hu", local.toChar(), parameters.localEndpoint.get_port());
            } else {
                (void)this->m_endpoint.format("connect %s:%hu", remote.toChar(), parameters.remoteEndpoint.get_port());
            }
            break;
        case ByteStreamTransport::UDP:
            if (parameters.hasLocal() && parameters.hasRemote()) {
                (void)this->m_endpoint.format("bind %s:%hu send %s:%hu", local.toChar(),
                                              parameters.localEndpoint.get_port(), remote.toChar(),
                                              parameters.remoteEndpoint.get_port());
            } else if (parameters.hasLocal()) {
                // Without a remote endpoint, UDP replies to whoever sent the last datagram
                (void)this->m_endpoint.format("bind %s:%hu", local.toChar(), parameters.localEndpoint.get_port());
            } else {
                (void)this->m_endpoint.format("send %s:%hu", remote.toChar(), parameters.remoteEndpoint.get_port());
            }
            break;
        case ByteStreamTransport::SERIAL:
            (void)this->m_endpoint.format("%s @ %u", parameters.serialDevice.toChar(),
                                          static_cast<unsigned int>(parameters.baudRate.e));
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
    if (this->m_transport == ByteStreamTransport::NONE) {
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

ByteStreamTransport UnifiedByteStreamDriver::getTransport() const {
    return this->m_transport;
}

U16 UnifiedByteStreamDriver::getLocalPort() {
    U16 port = 0;
    switch (this->m_transport.e) {
        case ByteStreamTransport::TCP:
            // A connecting socket has no local port of its own to report
            port = this->m_listening ? this->m_tcpServer.getListenPort() : static_cast<U16>(0);
            break;
        case ByteStreamTransport::UDP:
            port = this->m_receiveEnabled ? this->m_udp.getRecvPort() : static_cast<U16>(0);
            break;
        default:
            port = 0;  // A serial line has no local port to report
            break;
    }
    return port;
}

// ----------------------------------------------------------------------
// Implementations for socket read task virtual methods
// ----------------------------------------------------------------------

IpSocket& UnifiedByteStreamDriver::getSocketHandler() {
    switch (this->m_transport.e) {
        case ByteStreamTransport::TCP:
            if (this->m_listening) {
                return this->m_tcpServer;
            }
            return this->m_tcpClient;
        case ByteStreamTransport::UDP:
            return this->m_udp;
        case ByteStreamTransport::SERIAL:
            return this->m_serial;
        default:
            // Every path that reaches a transport is gated on a resolved configuration, so
            // an unconfigured driver arriving here is a coding error
            FW_ASSERT(false, static_cast<FwAssertArgType>(this->m_transport.e));
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
    this->log_ACTIVITY_HI_PortOpened(this->m_transport, this->m_endpoint);
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
    if ((this->m_transport == ByteStreamTransport::TCP) && this->m_listening) {
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
    if (this->m_transport == ByteStreamTransport::NONE) {
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
    this->tlmWrite_BytesRecv(this->m_bytesReceived);
    this->tlmWrite_Transport(this->m_transport);
    this->tlmWrite_Connected(this->isOpened());
}

}  // end namespace Drv
