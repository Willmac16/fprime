// ======================================================================
// \title  UnifiedByteStreamDriver.cpp
// \brief  cpp file for UnifiedByteStreamDriver component implementation class
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
    : UnifiedByteStreamDriverComponentBase(compName), SocketComponentHelper() {
    this->registerExternalParameters(this);
}

UnifiedByteStreamDriver::~UnifiedByteStreamDriver() {}

// ----------------------------------------------------------------------
// External parameters
// ----------------------------------------------------------------------

Fw::SerializeStatus UnifiedByteStreamDriver::deserializeParam(const FwPrmIdType base_id,
                                                              const FwPrmIdType local_id,
                                                              const Fw::ParamValid prmStat,
                                                              Fw::SerialBufferBase& buff) {
    if ((prmStat != Fw::ParamValid::VALID) && (prmStat != Fw::ParamValid::DEFAULT)) {
        return Fw::SerializeStatus::FW_DESERIALIZE_TYPE_MISMATCH;
    }
    Os::ScopeLock lock(this->m_paramLock);
    switch (local_id) {
        case PARAMID_TRANSPORT:
            return buff.deserializeTo(this->m_transportParam);
        case PARAMID_LOCAL_ENDPOINT:
            return buff.deserializeTo(this->m_localEndpoint);
        case PARAMID_REMOTE_ENDPOINT:
            return buff.deserializeTo(this->m_remoteEndpoint);
        case PARAMID_SERIAL_DEVICE:
            return buff.deserializeTo(this->m_serialDevice);
        case PARAMID_SERIAL_BAUD_RATE:
            return buff.deserializeTo(this->m_serialBaudRate);
        case PARAMID_SERIAL_PARITY:
            return buff.deserializeTo(this->m_serialParity);
        case PARAMID_SERIAL_FLOW_CONTROL:
            return buff.deserializeTo(this->m_serialFlowControl);
        case PARAMID_SERIAL_READ_TIMEOUT:
            return buff.deserializeTo(this->m_serialReadTimeout);
        case PARAMID_RECV_BUFFER_SIZE:
            return buff.deserializeTo(this->m_recvBufferSize);
        case PARAMID_SEND_TIMEOUT:
            return buff.deserializeTo(this->m_sendTimeout);
        default:
            FW_ASSERT(false, static_cast<FwAssertArgType>(local_id));
            break;
    }
    return Fw::SerializeStatus::FW_DESERIALIZE_TYPE_MISMATCH;
}

Fw::SerializeStatus UnifiedByteStreamDriver::serializeParam(const FwPrmIdType base_id,
                                                            const FwPrmIdType local_id,
                                                            Fw::SerialBufferBase& buff) const {
    Os::ScopeLock lock(this->m_paramLock);
    switch (local_id) {
        case PARAMID_TRANSPORT:
            return buff.serializeFrom(this->m_transportParam);
        case PARAMID_LOCAL_ENDPOINT:
            return buff.serializeFrom(this->m_localEndpoint);
        case PARAMID_REMOTE_ENDPOINT:
            return buff.serializeFrom(this->m_remoteEndpoint);
        case PARAMID_SERIAL_DEVICE:
            return buff.serializeFrom(this->m_serialDevice);
        case PARAMID_SERIAL_BAUD_RATE:
            return buff.serializeFrom(this->m_serialBaudRate);
        case PARAMID_SERIAL_PARITY:
            return buff.serializeFrom(this->m_serialParity);
        case PARAMID_SERIAL_FLOW_CONTROL:
            return buff.serializeFrom(this->m_serialFlowControl);
        case PARAMID_SERIAL_READ_TIMEOUT:
            return buff.serializeFrom(this->m_serialReadTimeout);
        case PARAMID_RECV_BUFFER_SIZE:
            return buff.serializeFrom(this->m_recvBufferSize);
        case PARAMID_SEND_TIMEOUT:
            return buff.serializeFrom(this->m_sendTimeout);
        default:
            FW_ASSERT(false, static_cast<FwAssertArgType>(local_id));
            break;
    }
    return Fw::SerializeStatus::FW_SERIALIZE_FORMAT_ERROR;
}

void UnifiedByteStreamDriver::setConfiguration(const ByteStreamTransport transport,
                                               const IpEndpoint& localEndpoint,
                                               const IpEndpoint& remoteEndpoint) {
    Os::ScopeLock lock(this->m_paramLock);
    this->m_transportParam = transport;
    this->m_localEndpoint = localEndpoint;
    this->m_remoteEndpoint = remoteEndpoint;
}

void UnifiedByteStreamDriver::setSerialConfiguration(const Fw::StringBase& device,
                                                     const SerialBaudRate baudRate,
                                                     const SerialParity parity,
                                                     const SerialFlowControl flowControl,
                                                     const U8 readTimeout) {
    Os::ScopeLock lock(this->m_paramLock);
    this->m_serialDevice = device;
    this->m_serialBaudRate = baudRate;
    this->m_serialParity = parity;
    this->m_serialFlowControl = flowControl;
    this->m_serialReadTimeout = readTimeout;
}

void UnifiedByteStreamDriver::setBufferConfiguration(const FwSizeType recvBufferSize, const SendTimeout& sendTimeout) {
    Os::ScopeLock lock(this->m_paramLock);
    this->m_recvBufferSize = recvBufferSize;
    this->m_sendTimeout = sendTimeout;
}

// ----------------------------------------------------------------------
// Configuration
// ----------------------------------------------------------------------

void UnifiedByteStreamDriver::formatAddress(const IpEndpoint& endpoint, Fw::String& address) {
    const IpEndpoint::Type_of_address& octets = endpoint.get_address();
    (void)address.format("%d.%d.%d.%d", octets[0], octets[1], octets[2], octets[3]);
}

bool UnifiedByteStreamDriver::hasAddress(const IpEndpoint& endpoint) {
    const IpEndpoint::Type_of_address& octets = endpoint.get_address();
    return (octets[0] != 0) || (octets[1] != 0) || (octets[2] != 0) || (octets[3] != 0);
}

bool UnifiedByteStreamDriver::hasRemote() const {
    return UnifiedByteStreamDriver::hasAddress(this->m_remoteEndpoint) || (this->m_remoteEndpoint.get_port() != 0);
}

bool UnifiedByteStreamDriver::remoteReachable() const {
    return UnifiedByteStreamDriver::hasAddress(this->m_remoteEndpoint) && (this->m_remoteEndpoint.get_port() != 0);
}

ByteStreamTransport UnifiedByteStreamDriver::reject(const ByteStreamConfigError error) const {
    Os::ScopeLock lock(this->m_downlinkLock);
    this->log_WARNING_HI_UnsupportedConfiguration(this->m_transportParam, error);
    return ByteStreamTransport::NONE;
}

ByteStreamTransport UnifiedByteStreamDriver::configure() {
    this->m_configured = true;

    // Checks that apply whatever the transport is
    if (this->m_recvBufferSize == 0) {
        this->m_transport = this->reject(ByteStreamConfigError::INVALID_BUFFER_SIZE);
        return this->m_transport;
    }
    if (this->m_sendTimeout.get_microseconds() >= 1000000) {
        this->m_transport = this->reject(ByteStreamConfigError::INVALID_SEND_TIMEOUT);
        return this->m_transport;
    }
    this->m_allocationSize = this->m_recvBufferSize;

    switch (this->m_transportParam.e) {
        case ByteStreamTransport::NONE: {
            Os::ScopeLock lock(this->m_downlinkLock);
            this->log_WARNING_HI_TransportNotConfigured();
            this->m_transport = ByteStreamTransport::NONE;
            break;
        }
        case ByteStreamTransport::TCP_CLIENT:
            this->m_transport = this->configureTcpClient();
            break;
        case ByteStreamTransport::TCP_SERVER:
            this->m_transport = this->configureTcpServer();
            break;
        case ByteStreamTransport::UDP:
            this->m_transport = this->configureUdp();
            break;
        case ByteStreamTransport::SERIAL:
            this->m_transport = this->configureSerial();
            break;
        default:
            this->m_transport = this->reject(ByteStreamConfigError::TRANSPORT_REJECTED_SETTINGS);
            break;
    }

    if (this->m_transport != ByteStreamTransport::NONE) {
        this->buildEndpoint();
        {
            Os::ScopeLock lock(this->m_downlinkLock);
            this->log_ACTIVITY_HI_ConfigurationApplied(this->m_transport, this->m_endpoint);
        }
    }
    this->reportConfiguration();
    return this->m_transport;
}

ByteStreamTransport UnifiedByteStreamDriver::configureTcpClient() {
    if (this->hasRemote() && (not this->remoteReachable())) {
        return this->reject(ByteStreamConfigError::INCOMPLETE_REMOTE_ENDPOINT);
    }
    if (not this->hasRemote()) {
        return this->reject(ByteStreamConfigError::MISSING_REMOTE_ENDPOINT);
    }

    Fw::String address;
    UnifiedByteStreamDriver::formatAddress(this->m_remoteEndpoint, address);
    SocketIpStatus status =
        this->m_tcpClient.configure(address.toChar(), this->m_remoteEndpoint.get_port(),
                                    this->m_sendTimeout.get_seconds(), this->m_sendTimeout.get_microseconds());
    if (status != SOCK_SUCCESS) {
        return this->reject(ByteStreamConfigError::TRANSPORT_REJECTED_SETTINGS);
    }

    UnifiedByteStreamDriver::formatAddress(this->m_localEndpoint, address);
    status = this->m_tcpClient.configureLocal(address.toChar(), this->m_localEndpoint.get_port());
    if (status != SOCK_SUCCESS) {
        return this->reject(ByteStreamConfigError::TRANSPORT_REJECTED_SETTINGS);
    }
    return ByteStreamTransport::TCP_CLIENT;
}

ByteStreamTransport UnifiedByteStreamDriver::configureTcpServer() {
    Fw::String address;
    UnifiedByteStreamDriver::formatAddress(this->m_localEndpoint, address);
    const SocketIpStatus status =
        this->m_tcpServer.configure(address.toChar(), this->m_localEndpoint.get_port(),
                                    this->m_sendTimeout.get_seconds(), this->m_sendTimeout.get_microseconds());
    if (status != SOCK_SUCCESS) {
        return this->reject(ByteStreamConfigError::TRANSPORT_REJECTED_SETTINGS);
    }
    return ByteStreamTransport::TCP_SERVER;
}

ByteStreamTransport UnifiedByteStreamDriver::configureUdp() {
    if (this->hasRemote() && (not this->remoteReachable())) {
        return this->reject(ByteStreamConfigError::INCOMPLETE_REMOTE_ENDPOINT);
    }

    Fw::String address;
    UnifiedByteStreamDriver::formatAddress(this->m_localEndpoint, address);
    SocketIpStatus status = this->m_udp.configureRecv(address.toChar(), this->m_localEndpoint.get_port());
    if (status != SOCK_SUCCESS) {
        return this->reject(ByteStreamConfigError::TRANSPORT_REJECTED_SETTINGS);
    }

    // Without a destination the socket replies to whoever sent the last datagram
    if (this->hasRemote()) {
        UnifiedByteStreamDriver::formatAddress(this->m_remoteEndpoint, address);
        status = this->m_udp.configureSend(address.toChar(), this->m_remoteEndpoint.get_port(),
                                           this->m_sendTimeout.get_seconds(), this->m_sendTimeout.get_microseconds());
        if (status != SOCK_SUCCESS) {
            return this->reject(ByteStreamConfigError::TRANSPORT_REJECTED_SETTINGS);
        }
    }
    return ByteStreamTransport::UDP;
}

ByteStreamTransport UnifiedByteStreamDriver::configureSerial() {
    if (this->m_serialDevice.length() == 0) {
        return this->reject(ByteStreamConfigError::NO_SERIAL_DEVICE);
    }
    if (not SerialStream::isBaudRateSupported(this->m_serialBaudRate)) {
        return this->reject(ByteStreamConfigError::UNSUPPORTED_BAUD_RATE);
    }
    const SocketIpStatus status =
        this->m_serial.configureSerial(this->m_serialDevice.toChar(), this->m_serialBaudRate, this->m_serialParity,
                                       this->m_serialFlowControl, this->m_serialReadTimeout);
    if (status != SOCK_SUCCESS) {
        return this->reject(ByteStreamConfigError::TRANSPORT_REJECTED_SETTINGS);
    }
    return ByteStreamTransport::SERIAL;
}

void UnifiedByteStreamDriver::buildEndpoint() {
    Os::ScopeLock lock(this->m_paramLock);
    // A wildcard local port is only resolved when the transport opens, so prefer the port
    // actually bound over the requested one
    const U16 bound = this->getLocalPort();
    const U16 localPort = (bound != 0) ? bound : this->m_localEndpoint.get_port();
    const U16 remotePort = this->m_remoteEndpoint.get_port();
    Fw::String local;
    Fw::String remote;
    UnifiedByteStreamDriver::formatAddress(this->m_localEndpoint, local);
    UnifiedByteStreamDriver::formatAddress(this->m_remoteEndpoint, remote);
    switch (this->m_transport.e) {
        case ByteStreamTransport::TCP_CLIENT:
            (void)this->m_endpoint.format("connect %s:%hu", remote.toChar(), remotePort);
            break;
        case ByteStreamTransport::TCP_SERVER:
            (void)this->m_endpoint.format("listen %s:%hu", local.toChar(), localPort);
            break;
        case ByteStreamTransport::UDP:
            if (this->hasRemote()) {
                (void)this->m_endpoint.format("bind %s:%hu send %s:%hu", local.toChar(), localPort, remote.toChar(),
                                              remotePort);
            } else {
                (void)this->m_endpoint.format("bind %s:%hu", local.toChar(), localPort);
            }
            break;
        case ByteStreamTransport::SERIAL:
            (void)this->m_endpoint.format("%s @ %u", this->m_serialDevice.toChar(),
                                          static_cast<unsigned int>(this->m_serialBaudRate.e));
            break;
        default:
            this->m_endpoint = "";
            break;
    }
}

void UnifiedByteStreamDriver::reportConfiguration() {
    Os::ScopeLock lock(this->m_downlinkLock);
    this->tlmWrite_Transport(this->m_transport);
    this->tlmWrite_LocalEndpoint(this->m_localEndpoint);
    this->tlmWrite_RemoteEndpoint(this->m_remoteEndpoint);
    this->tlmWrite_SerialDevice(this->m_serialDevice);
    this->tlmWrite_SerialBaudRate(this->m_serialBaudRate);
    this->tlmWrite_SerialParity(this->m_serialParity);
    this->tlmWrite_SerialFlowControl(this->m_serialFlowControl);
    this->tlmWrite_SerialReadTimeout(this->m_serialReadTimeout);
    this->tlmWrite_RecvBufferSize(this->m_recvBufferSize);
    this->tlmWrite_SendTimeout(this->m_sendTimeout);
    this->tlmWrite_LocalPort(this->getLocalPort());
    // Connected is not written here: this runs under the configuration lock, and asking the
    // helper whether it is open takes the helper's lock, which is the opposite of the order
    // the helper takes them in. It is written where the state actually changes instead.
}

void UnifiedByteStreamDriver::parametersLoaded() {
    (void)this->configure();
}

void UnifiedByteStreamDriver::parameterUpdated(FwPrmIdType id) {
    if (not this->m_configured) {
        return;  // Nothing is up yet, so the first configure will pick this value up
    }
    // The read task owns the transport while it runs, so it comes down before the
    // configuration is rebuilt and goes back up the way it went up
    const bool wasStarted = this->m_started;
    if (wasStarted) {
        this->stop();
        (void)this->join();
    }
    (void)this->configure();
    {
        Os::ScopeLock lock(this->m_downlinkLock);
        this->log_ACTIVITY_HI_ConfigurationReloaded();
    }
    if (wasStarted && (this->m_transport != ByteStreamTransport::NONE)) {
        this->start(this->m_priority, this->m_stack, this->m_cpuAffinity);
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
    this->m_priority = priority;
    this->m_stack = stack;
    this->m_cpuAffinity = cpuAffinity;
    this->m_started = true;
    this->m_serial.clearStop();
    SocketComponentHelper::start(Fw::String(this->getObjName()), priority, stack, cpuAffinity);
}

void UnifiedByteStreamDriver::stop() {
    if (not this->m_started) {
        return;
    }
    SocketComponentHelper::stop();
}

Os::Task::Status UnifiedByteStreamDriver::join() {
    if (not this->m_started) {
        return Os::Task::Status::OP_OK;  // nothing was ever started to join with
    }
    const Os::Task::Status status = SocketComponentHelper::join();
    this->m_started = false;
    return status;
}

ByteStreamTransport UnifiedByteStreamDriver::getTransport() const {
    return this->m_transport;
}

U16 UnifiedByteStreamDriver::getLocalPort() {
    U16 port = 0;
    switch (this->m_transport.e) {
        case ByteStreamTransport::TCP_SERVER:
            port = this->m_tcpServer.getListenPort();
            break;
        case ByteStreamTransport::UDP:
            port = this->m_udp.getRecvPort();
            break;
        default:
            // A TCP client and a serial line bind no port this driver can report
            port = 0;
            break;
    }
    return port;
}

// ----------------------------------------------------------------------
// Implementations for socket read task virtual methods
// ----------------------------------------------------------------------

IpSocket& UnifiedByteStreamDriver::getSocketHandler() {
    IpSocket* socket = nullptr;
    switch (this->m_transport.e) {
        case ByteStreamTransport::TCP_CLIENT:
            socket = &this->m_tcpClient;
            break;
        case ByteStreamTransport::TCP_SERVER:
            socket = &this->m_tcpServer;
            break;
        case ByteStreamTransport::UDP:
            socket = &this->m_udp;
            break;
        case ByteStreamTransport::SERIAL:
            socket = &this->m_serial;
            break;
        default:
            break;
    }
    FW_ASSERT(socket != nullptr, static_cast<FwAssertArgType>(this->m_transport.e));
    return *socket;
}

Fw::Buffer UnifiedByteStreamDriver::getBuffer() {
    Fw::Buffer buffer = this->allocate_out(0, this->m_allocationSize);
    if (not buffer.isValid()) {
        // A zero-size buffer is not valid but may still own memory, so hand it back rather
        // than dropping it on the floor
        if (buffer.getData() != nullptr) {
            this->deallocate_out(0, buffer);
        }
        {
            Os::ScopeLock lock(this->m_downlinkLock);
            this->log_WARNING_HI_NoBuffers();
        }
        return Fw::Buffer();
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
        {
            Os::ScopeLock lock(this->m_downlinkLock);
            this->tlmWrite_BytesRecv(this->m_bytesReceived);
        }
    } else if (status == SOCK_NO_DATA_AVAILABLE) {
        recvStatus = ByteStreamStatus::RECV_NO_DATA;
    } else {
        recvStatus = ByteStreamStatus::OTHER_ERROR;
        Os::ScopeLock lock(this->m_downlinkLock);
        this->log_WARNING_LO_ReceiveError(static_cast<I32>(status));
    }
    this->recv_out(0, buffer, recvStatus);
}

void UnifiedByteStreamDriver::connected() {
    this->buildEndpoint();  // an ephemeral port only has a value now
    {
        Os::ScopeLock lock(this->m_downlinkLock);
        this->log_ACTIVITY_HI_PortOpened(this->m_transport, this->m_endpoint);
        this->tlmWrite_LocalPort(this->getLocalPort());
        this->tlmWrite_Connected(true);
    }
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

void UnifiedByteStreamDriver::readLoop() {
    // A listening socket has to be brought up before the helper's loop can accept on it,
    // and torn down after. This mirrors Drv::TcpServerComponentImpl, which is where the
    // logic lives: Drv::Ip carries the socket, not the listen lifecycle.
    if (this->m_transport == ByteStreamTransport::TCP_SERVER) {
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
    } else {
        SocketComponentHelper::readLoop();
    }
    {
        Os::ScopeLock lock(this->m_downlinkLock);
        this->tlmWrite_Connected(false);
    }
}

// ----------------------------------------------------------------------
// Handler implementations for user-defined typed input ports
// ----------------------------------------------------------------------

Drv::ByteStreamStatus UnifiedByteStreamDriver::send_handler(const FwIndexType portNum, Fw::Buffer& fwBuffer) {
    if (this->m_transport == ByteStreamTransport::NONE) {
        // Worth an event of its own: a disabled driver silently swallowing sends is exactly
        // the failure that is hard to see from the ground
        Os::ScopeLock lock(this->m_downlinkLock);
        this->log_WARNING_LO_SendError(static_cast<I32>(SOCK_NOT_STARTED));
        return ByteStreamStatus::OTHER_ERROR;
    }
    const FwSizeType size = fwBuffer.getSize();
    const Drv::SocketIpStatus status = this->send(fwBuffer.getData(), size);
    Drv::ByteStreamStatus returnStatus = ByteStreamStatus::OTHER_ERROR;
    switch (status) {
        case SOCK_SUCCESS:
            this->m_bytesSent += size;
            {
                Os::ScopeLock lock(this->m_downlinkLock);
                this->tlmWrite_BytesSent(this->m_bytesSent);
            }
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
        Os::ScopeLock lock(this->m_downlinkLock);
        this->log_WARNING_LO_SendError(static_cast<I32>(status));
    }
    return returnStatus;
}

void UnifiedByteStreamDriver::recvReturnIn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) {
    this->deallocate_out(0, fwBuffer);
}

}  // end namespace Drv
