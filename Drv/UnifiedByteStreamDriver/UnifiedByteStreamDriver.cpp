// ======================================================================
// \title  UnifiedByteStreamDriver.cpp
// \brief  cpp file for UnifiedByteStreamDriver component implementation class
// ======================================================================

#include "UnifiedByteStreamDriver.hpp"
#include <Fw/Logger/Logger.hpp>
#include <Fw/Types/Assert.hpp>
#include <config/IpCfg.hpp>

namespace Drv {

namespace {
//! Shortest gap between two reports of a condition that can persist, in seconds. An empty
//! allocator or a link that will not come up repeats for as long as it lasts, so these are
//! limited on elapsed time rather than capped at a count that would stop reporting for good.
constexpr U32 EVENT_RATE_LIMIT_SECONDS = 5;
}  // namespace

// ----------------------------------------------------------------------
// Construction, initialization, and destruction
// ----------------------------------------------------------------------

UnifiedByteStreamDriver::UnifiedByteStreamDriver(const char* const compName)
    : UnifiedByteStreamDriverComponentBase(compName), SocketComponentHelper() {
    this->registerExternalParameters(this);
    this->m_downlink.noBuffers.setTimeCycle(EVENT_RATE_LIMIT_SECONDS);
    this->m_downlink.sendError.setTimeCycle(EVENT_RATE_LIMIT_SECONDS);
    this->m_downlink.receiveError.setTimeCycle(EVENT_RATE_LIMIT_SECONDS);
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
    Os::ScopeLock lock(this->m_config.lock);
    switch (local_id) {
        case PARAMID_TRANSPORT:
            return buff.deserializeTo(this->m_config.transportParam);
        case PARAMID_LOCAL_ENDPOINT:
            return buff.deserializeTo(this->m_config.localEndpoint);
        case PARAMID_REMOTE_ENDPOINT:
            return buff.deserializeTo(this->m_config.remoteEndpoint);
        case PARAMID_SERIAL_CONFIG:
            return buff.deserializeTo(this->m_config.serial);
        case PARAMID_RECV_BUFFER_SIZE:
            return buff.deserializeTo(this->m_config.recvBufferSize);
        case PARAMID_SEND_TIMEOUT:
            return buff.deserializeTo(this->m_config.sendTimeout);
        default:
            FW_ASSERT(false, static_cast<FwAssertArgType>(local_id));
            break;
    }
    return Fw::SerializeStatus::FW_DESERIALIZE_TYPE_MISMATCH;
}

Fw::SerializeStatus UnifiedByteStreamDriver::serializeParam(const FwPrmIdType base_id,
                                                            const FwPrmIdType local_id,
                                                            Fw::SerialBufferBase& buff) const {
    Os::ScopeLock lock(this->m_config.lock);
    switch (local_id) {
        case PARAMID_TRANSPORT:
            return buff.serializeFrom(this->m_config.transportParam);
        case PARAMID_LOCAL_ENDPOINT:
            return buff.serializeFrom(this->m_config.localEndpoint);
        case PARAMID_REMOTE_ENDPOINT:
            return buff.serializeFrom(this->m_config.remoteEndpoint);
        case PARAMID_SERIAL_CONFIG:
            return buff.serializeFrom(this->m_config.serial);
        case PARAMID_RECV_BUFFER_SIZE:
            return buff.serializeFrom(this->m_config.recvBufferSize);
        case PARAMID_SEND_TIMEOUT:
            return buff.serializeFrom(this->m_config.sendTimeout);
        default:
            FW_ASSERT(false, static_cast<FwAssertArgType>(local_id));
            break;
    }
    return Fw::SerializeStatus::FW_SERIALIZE_FORMAT_ERROR;
}

void UnifiedByteStreamDriver::setConfiguration(const ByteStreamTransport transport,
                                               const IpEndpoint& localEndpoint,
                                               const IpEndpoint& remoteEndpoint) {
    Os::ScopeLock lock(this->m_config.lock);
    this->m_config.transportParam = transport;
    this->m_config.localEndpoint = localEndpoint;
    this->m_config.remoteEndpoint = remoteEndpoint;
}

void UnifiedByteStreamDriver::setSerialConfiguration(const SerialConfig& serialConfig) {
    Os::ScopeLock lock(this->m_config.lock);
    this->m_config.serial = serialConfig;
}

void UnifiedByteStreamDriver::setBufferConfiguration(const FwSizeType recvBufferSize, const SendTimeout& sendTimeout) {
    Os::ScopeLock lock(this->m_config.lock);
    this->m_config.recvBufferSize = recvBufferSize;
    this->m_config.sendTimeout = sendTimeout;
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
    return UnifiedByteStreamDriver::hasAddress(this->m_config.remoteEndpoint) ||
           (this->m_config.remoteEndpoint.get_port() != 0);
}

bool UnifiedByteStreamDriver::remoteReachable() const {
    return UnifiedByteStreamDriver::hasAddress(this->m_config.remoteEndpoint) &&
           (this->m_config.remoteEndpoint.get_port() != 0);
}

ByteStreamTransport UnifiedByteStreamDriver::reject(const ByteStreamConfigError error) const {
    Os::ScopeLock lock(this->m_downlink.lock);
    this->log_WARNING_HI_UnsupportedConfiguration(this->m_config.transportParam, error);
    return ByteStreamTransport::NONE;
}

ByteStreamTransport UnifiedByteStreamDriver::configure() {
    Os::ScopeLock lock(this->m_config.lock);
    return this->applyConfiguration();
}

ByteStreamTransport UnifiedByteStreamDriver::applyConfiguration() {
    this->m_config.reconfigurePending = false;
    this->m_config.resolved = true;

    if (this->m_config.recvBufferSize == 0) {
        this->m_config.transport = this->reject(ByteStreamConfigError::INVALID_BUFFER_SIZE);
        return this->m_config.transport;
    }
    if (this->m_config.sendTimeout.get_microseconds() >= 1000000) {
        this->m_config.transport = this->reject(ByteStreamConfigError::INVALID_SEND_TIMEOUT);
        return this->m_config.transport;
    }
    this->m_config.allocationSize = this->m_config.recvBufferSize;

    switch (this->m_config.transportParam.e) {
        case ByteStreamTransport::NONE: {
            Os::ScopeLock lock(this->m_downlink.lock);
            this->log_WARNING_HI_TransportNotConfigured();
            this->m_config.transport = ByteStreamTransport::NONE;
            break;
        }
        case ByteStreamTransport::TCP_CLIENT:
            this->m_config.transport = this->configureTcpClient();
            break;
        case ByteStreamTransport::TCP_SERVER:
            this->m_config.transport = this->configureTcpServer();
            break;
        case ByteStreamTransport::UDP:
            this->m_config.transport = this->configureUdp();
            break;
        case ByteStreamTransport::SERIAL:
            this->m_config.transport = this->configureSerial();
            break;
        default:
            this->m_config.transport = this->reject(ByteStreamConfigError::TRANSPORT_REJECTED_SETTINGS);
            break;
    }

    if (this->m_config.transport != ByteStreamTransport::NONE) {
        this->buildEndpoint();
        {
            Os::ScopeLock lock(this->m_downlink.lock);
            this->log_ACTIVITY_HI_ConfigurationApplied(this->m_config.transport, this->m_config.endpoint);
        }
    }
    this->reportConfiguration();
    return this->m_config.transport;
}

ByteStreamTransport UnifiedByteStreamDriver::configureTcpClient() {
    if (this->hasRemote() && (not this->remoteReachable())) {
        return this->reject(ByteStreamConfigError::INCOMPLETE_REMOTE_ENDPOINT);
    }
    if (not this->hasRemote()) {
        return this->reject(ByteStreamConfigError::MISSING_REMOTE_ENDPOINT);
    }

    Fw::String address;
    UnifiedByteStreamDriver::formatAddress(this->m_config.remoteEndpoint, address);
    const SocketIpStatus status = this->m_tcpClient.configure(
        address.toChar(), this->m_config.remoteEndpoint.get_port(), this->m_config.sendTimeout.get_seconds(),
        this->m_config.sendTimeout.get_microseconds());
    if (status != SOCK_SUCCESS) {
        return this->reject(ByteStreamConfigError::TRANSPORT_REJECTED_SETTINGS);
    }
    return ByteStreamTransport::TCP_CLIENT;
}

ByteStreamTransport UnifiedByteStreamDriver::configureTcpServer() {
    Fw::String address;
    UnifiedByteStreamDriver::formatAddress(this->m_config.localEndpoint, address);
    const SocketIpStatus status = this->m_tcpServer.configure(
        address.toChar(), this->m_config.localEndpoint.get_port(), this->m_config.sendTimeout.get_seconds(),
        this->m_config.sendTimeout.get_microseconds());
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
    UnifiedByteStreamDriver::formatAddress(this->m_config.localEndpoint, address);
    SocketIpStatus status = this->m_udp.configureRecv(address.toChar(), this->m_config.localEndpoint.get_port());
    if (status != SOCK_SUCCESS) {
        return this->reject(ByteStreamConfigError::TRANSPORT_REJECTED_SETTINGS);
    }

    // Without a destination the socket replies to whoever sent the last datagram
    if (this->hasRemote()) {
        UnifiedByteStreamDriver::formatAddress(this->m_config.remoteEndpoint, address);
        status = this->m_udp.configureSend(address.toChar(), this->m_config.remoteEndpoint.get_port(),
                                           this->m_config.sendTimeout.get_seconds(),
                                           this->m_config.sendTimeout.get_microseconds());
        if (status != SOCK_SUCCESS) {
            return this->reject(ByteStreamConfigError::TRANSPORT_REJECTED_SETTINGS);
        }
    }
    return ByteStreamTransport::UDP;
}

ByteStreamTransport UnifiedByteStreamDriver::configureSerial() {
    if (this->m_config.serial.get_device().length() == 0) {
        return this->reject(ByteStreamConfigError::NO_SERIAL_DEVICE);
    }
    if (not SerialStream::isBaudRateSupported(this->m_config.serial.get_baudRate())) {
        return this->reject(ByteStreamConfigError::UNSUPPORTED_BAUD_RATE);
    }
    const SocketIpStatus status = this->m_serial.configureSerial(
        this->m_config.serial.get_device().toChar(), this->m_config.serial.get_baudRate(),
        this->m_config.serial.get_parity(), this->m_config.serial.get_flowControl(),
        this->m_config.serial.get_readTimeout());
    if (status != SOCK_SUCCESS) {
        return this->reject(ByteStreamConfigError::TRANSPORT_REJECTED_SETTINGS);
    }
    return ByteStreamTransport::SERIAL;
}

void UnifiedByteStreamDriver::buildEndpoint() {
    // A wildcard local port is only resolved when the transport opens, so prefer the port
    // actually bound over the requested one
    const U16 bound = this->getLocalPort();
    const U16 localPort = (bound != 0) ? bound : this->m_config.localEndpoint.get_port();
    const U16 remotePort = this->m_config.remoteEndpoint.get_port();
    Fw::String local;
    Fw::String remote;
    UnifiedByteStreamDriver::formatAddress(this->m_config.localEndpoint, local);
    UnifiedByteStreamDriver::formatAddress(this->m_config.remoteEndpoint, remote);
    switch (this->m_config.transport.e) {
        case ByteStreamTransport::TCP_CLIENT:
            (void)this->m_config.endpoint.format("connect %s:%hu", remote.toChar(), remotePort);
            break;
        case ByteStreamTransport::TCP_SERVER:
            (void)this->m_config.endpoint.format("listen %s:%hu", local.toChar(), localPort);
            break;
        case ByteStreamTransport::UDP:
            if (this->hasRemote()) {
                (void)this->m_config.endpoint.format("bind %s:%hu send %s:%hu", local.toChar(), localPort,
                                                     remote.toChar(), remotePort);
            } else {
                (void)this->m_config.endpoint.format("bind %s:%hu", local.toChar(), localPort);
            }
            break;
        case ByteStreamTransport::SERIAL:
            (void)this->m_config.endpoint.format("%s @ %u", this->m_config.serial.get_device().toChar(),
                                                 static_cast<unsigned int>(this->m_config.serial.get_baudRate()));
            break;
        default:
            this->m_config.endpoint = "";
            break;
    }
}

void UnifiedByteStreamDriver::reportConfiguration() {
    Os::ScopeLock lock(this->m_downlink.lock);
    this->tlmWrite_ActiveTransport(this->m_config.transport);
    this->tlmWrite_TRANSPORT(this->m_config.transportParam);
    this->tlmWrite_LOCAL_ENDPOINT(this->m_config.localEndpoint);
    this->tlmWrite_REMOTE_ENDPOINT(this->m_config.remoteEndpoint);
    this->tlmWrite_SERIAL_CONFIG(this->m_config.serial);
    this->tlmWrite_RECV_BUFFER_SIZE(this->m_config.recvBufferSize);
    this->tlmWrite_SEND_TIMEOUT(this->m_config.sendTimeout);
    this->tlmWrite_LocalPort(this->getLocalPort());
}

void UnifiedByteStreamDriver::parametersLoaded() {
    this->parameterUpdated(PARAMID_TRANSPORT);
    this->parameterUpdated(PARAMID_LOCAL_ENDPOINT);
    this->parameterUpdated(PARAMID_REMOTE_ENDPOINT);
    this->parameterUpdated(PARAMID_SERIAL_CONFIG);
    this->parameterUpdated(PARAMID_RECV_BUFFER_SIZE);
    this->parameterUpdated(PARAMID_SEND_TIMEOUT);
}

void UnifiedByteStreamDriver::parameterUpdated(FwPrmIdType id) {
    // Every parameter feeds the same one resolution, so which of them changed does not
    // matter here. All this does is decide when that resolution can happen.
    ByteStreamTransport live = ByteStreamTransport::NONE;
    {
        Os::ScopeLock lock(this->m_config.lock);
        this->m_config.reconfigurePending = true;
        if (not this->m_config.resolved) {
            // Parameters are still arriving, one call to here each. Resolving now would do
            // it once per parameter; start() resolves the set of them once instead.
            return;
        }
        if (not this->m_config.started) {
            (void)this->applyConfiguration();
            Os::ScopeLock downlink(this->m_downlink.lock);
            this->log_ACTIVITY_HI_ConfigurationReloaded();
            return;
        }
        // The teardown below reaches getSocketHandler on this thread, where applying the
        // change would be the very cross-thread write this hand-off exists to avoid
        this->m_config.suppressApply = true;
        live = this->m_config.transport;
    }
    // A read blocked on a socket does not notice a close, so break it out first. A blocked
    // serial read is not a socket to shut down - it returns on its own read timeout - and
    // shutting it down would close the descriptor the close below owns.
    if (live != ByteStreamTransport::SERIAL) {
        this->shutdown();
    }
    this->close();
    {
        Os::ScopeLock lock(this->m_config.lock);
        this->m_config.suppressApply = false;
    }
}

// ----------------------------------------------------------------------
// Lifecycle
// ----------------------------------------------------------------------

void UnifiedByteStreamDriver::start(const FwTaskPriorityType priority,
                                    const Os::Task::ParamType stack,
                                    const Os::Task::ParamType cpuAffinity) {
    {
        Os::ScopeLock lock(this->m_config.lock);
        FW_ASSERT(not this->m_config.started);  // It is a coding error to start the driver twice
        const ByteStreamTransport transport =
            (this->m_config.reconfigurePending || (not this->m_config.resolved)) ? this->applyConfiguration()
                                                                                 : this->m_config.transport;
        if (transport == ByteStreamTransport::NONE) {
            return;  // Nothing to run: the rejection has already been reported
        }
        this->m_config.started = true;
    }
    this->m_serial.clearStop();
    SocketComponentHelper::start(Fw::String(this->getObjName()), priority, stack, cpuAffinity);
}

bool UnifiedByteStreamDriver::isStarted() const {
    Os::ScopeLock lock(this->m_config.lock);
    return this->m_config.started;
}

void UnifiedByteStreamDriver::stop() {
    if (not this->isStarted()) {
        return;
    }
    if (this->getTransport() == ByteStreamTransport::SERIAL) {
        // SocketComponentHelper::stop would shut the descriptor down, and IpSocket::shutdown
        // closes what ::shutdown could not shut - every tty - without clearing the descriptor
        // the helper still holds and later closes again. Stop the task the same way, minus
        // that shutdown: a blocked serial read returns on its own read timeout.
        {
            Os::ScopeLock lock(this->m_lock);
            this->m_stop = true;
        }
        this->stopReconnect();
        this->m_serial.requestStop();
    } else {
        SocketComponentHelper::stop();
    }
}

Os::Task::Status UnifiedByteStreamDriver::join() {
    if (not this->isStarted()) {
        return Os::Task::Status::OP_OK;
    }
    const Os::Task::Status status = SocketComponentHelper::join();
    {
        Os::ScopeLock lock(this->m_config.lock);
        this->m_config.started = false;
    }
    return status;
}

ByteStreamTransport UnifiedByteStreamDriver::getTransport() const {
    Os::ScopeLock lock(this->m_config.lock);
    return this->m_config.transport;
}

U16 UnifiedByteStreamDriver::getLocalPort() {
    U16 port = 0;
    switch (this->m_config.transport.e) {
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
    Os::ScopeLock lock(this->m_config.lock);
    // The read and reconnect tasks come through here before every open and every receive,
    // which is where a configuration staged by parameterUpdated is safe to apply
    if (this->m_config.reconfigurePending && (not this->m_config.suppressApply)) {
        const ByteStreamTransport previous = this->m_config.transport;
        (void)this->applyConfiguration();
        if (this->m_config.transport == ByteStreamTransport::NONE) {
            // A change that resolves to nothing must not take a working link down with it
            this->m_config.transport = previous;
        } else {
            Os::ScopeLock downlink(this->m_downlink.lock);
            this->log_ACTIVITY_HI_ConfigurationReloaded();
        }
    }
    IpSocket* socket = nullptr;
    switch (this->m_config.transport.e) {
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
    FW_ASSERT(socket != nullptr, static_cast<FwAssertArgType>(this->m_config.transport.e));
    return *socket;
}

Fw::Buffer UnifiedByteStreamDriver::getBuffer() {
    FwSizeType size = 0;
    {
        Os::ScopeLock lock(this->m_config.lock);
        size = this->m_config.allocationSize;
    }
    Fw::Buffer buffer = this->allocate_out(0, size);
    if (not buffer.isValid()) {
        // A zero-size buffer is not valid but may still own memory, so hand it back rather
        // than dropping it on the floor
        if (buffer.getData() != nullptr) {
            this->deallocate_out(0, buffer);
        }
        {
            Os::ScopeLock lock(this->m_downlink.lock);
            if (this->m_downlink.noBuffers.trigger(this->getTime())) {
                this->log_WARNING_HI_NoBuffers();
            }
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
        Os::ScopeLock lock(this->m_downlink.lock);
        this->m_downlink.bytesReceived += buffer.getSize();
        this->tlmWrite_BytesRecv(this->m_downlink.bytesReceived);
    } else if (status == SOCK_NO_DATA_AVAILABLE) {
        recvStatus = ByteStreamStatus::RECV_NO_DATA;
    } else {
        recvStatus = ByteStreamStatus::OTHER_ERROR;
        Os::ScopeLock lock(this->m_downlink.lock);
        if (this->m_downlink.receiveError.trigger(this->getTime())) {
            this->log_WARNING_LO_ReceiveError(static_cast<I32>(status));
        }
    }
    this->recv_out(0, buffer, recvStatus);
}

void UnifiedByteStreamDriver::connected() {
    ByteStreamTransport transport = ByteStreamTransport::NONE;
    Fw::String endpoint;
    U16 localPort = 0;
    {
        // The endpoint description is built from the parameter storage, which a command
        // can be writing at the same moment
        Os::ScopeLock lock(this->m_config.lock);
        this->buildEndpoint();  // an ephemeral port only has a value now
        transport = this->m_config.transport;
        endpoint = this->m_config.endpoint;
        localPort = this->getLocalPort();
    }
    {
        Os::ScopeLock lock(this->m_downlink.lock);
        this->log_ACTIVITY_HI_PortOpened(transport, endpoint);
        this->tlmWrite_LocalPort(localPort);
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
    if (this->getTransport() == ByteStreamTransport::TCP_SERVER) {
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
        Os::ScopeLock lock(this->m_downlink.lock);
        this->tlmWrite_Connected(false);
    }
}

// ----------------------------------------------------------------------
// Handler implementations for user-defined typed input ports
// ----------------------------------------------------------------------

Drv::ByteStreamStatus UnifiedByteStreamDriver::send_handler(const FwIndexType portNum, Fw::Buffer& fwBuffer) {
    if (this->getTransport() == ByteStreamTransport::NONE) {
        // Worth reporting: a disabled driver silently swallowing sends is exactly the
        // failure that is hard to see from the ground
        Os::ScopeLock lock(this->m_downlink.lock);
        if (this->m_downlink.sendError.trigger(this->getTime())) {
            this->log_WARNING_LO_SendError(static_cast<I32>(SOCK_NOT_STARTED));
        }
        return ByteStreamStatus::OTHER_ERROR;
    }
    const FwSizeType size = fwBuffer.getSize();
    const Drv::SocketIpStatus status = this->send(fwBuffer.getData(), size);
    Drv::ByteStreamStatus returnStatus = ByteStreamStatus::OTHER_ERROR;
    switch (status) {
        case SOCK_SUCCESS: {
            Os::ScopeLock lock(this->m_downlink.lock);
            this->m_downlink.bytesSent += size;
            this->tlmWrite_BytesSent(this->m_downlink.bytesSent);
            returnStatus = ByteStreamStatus::OP_OK;
            break;
        }
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
        Os::ScopeLock lock(this->m_downlink.lock);
        if (this->m_downlink.sendError.trigger(this->getTime())) {
            this->log_WARNING_LO_SendError(static_cast<I32>(status));
        }
    }
    return returnStatus;
}

void UnifiedByteStreamDriver::recvReturnIn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) {
    this->deallocate_out(0, fwBuffer);
}

}  // end namespace Drv
