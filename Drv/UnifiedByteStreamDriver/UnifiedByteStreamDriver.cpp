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
    : UnifiedByteStreamDriverComponentBase(compName), SocketComponentHelper() {}

UnifiedByteStreamDriver::~UnifiedByteStreamDriver() {}

// ----------------------------------------------------------------------
// Configuration
// ----------------------------------------------------------------------

// Drv.SocketStatus mirrors Drv::SocketIpStatus, which has no FPP declaration of its own. A
// change to either fails here rather than mislabelling a status on the ground.
static_assert(static_cast<I32>(SocketStatus::SUCCESS) == SOCK_SUCCESS, "SocketStatus drifted");
static_assert(static_cast<I32>(SocketStatus::FAILED_TO_GET_SOCKET) == SOCK_FAILED_TO_GET_SOCKET,
              "SocketStatus drifted");
static_assert(static_cast<I32>(SocketStatus::FAILED_TO_GET_HOST_IP) == SOCK_FAILED_TO_GET_HOST_IP,
              "SocketStatus drifted");
static_assert(static_cast<I32>(SocketStatus::INVALID_IP_ADDRESS) == SOCK_INVALID_IP_ADDRESS, "SocketStatus drifted");
static_assert(static_cast<I32>(SocketStatus::FAILED_TO_CONNECT) == SOCK_FAILED_TO_CONNECT, "SocketStatus drifted");
static_assert(static_cast<I32>(SocketStatus::FAILED_TO_SET_SOCKET_OPTIONS) == SOCK_FAILED_TO_SET_SOCKET_OPTIONS,
              "SocketStatus drifted");
static_assert(static_cast<I32>(SocketStatus::INTERRUPTED_TRY_AGAIN) == SOCK_INTERRUPTED_TRY_AGAIN,
              "SocketStatus drifted");
static_assert(static_cast<I32>(SocketStatus::READ_ERROR) == SOCK_READ_ERROR, "SocketStatus drifted");
static_assert(static_cast<I32>(SocketStatus::DISCONNECTED) == SOCK_DISCONNECTED, "SocketStatus drifted");
static_assert(static_cast<I32>(SocketStatus::FAILED_TO_BIND) == SOCK_FAILED_TO_BIND, "SocketStatus drifted");
static_assert(static_cast<I32>(SocketStatus::FAILED_TO_LISTEN) == SOCK_FAILED_TO_LISTEN, "SocketStatus drifted");
static_assert(static_cast<I32>(SocketStatus::FAILED_TO_ACCEPT) == SOCK_FAILED_TO_ACCEPT, "SocketStatus drifted");
static_assert(static_cast<I32>(SocketStatus::SEND_ERROR) == SOCK_SEND_ERROR, "SocketStatus drifted");
static_assert(static_cast<I32>(SocketStatus::NOT_STARTED) == SOCK_NOT_STARTED, "SocketStatus drifted");
static_assert(static_cast<I32>(SocketStatus::FAILED_TO_READ_BACK_PORT) == SOCK_FAILED_TO_READ_BACK_PORT,
              "SocketStatus drifted");
static_assert(static_cast<I32>(SocketStatus::NO_DATA_AVAILABLE) == SOCK_NO_DATA_AVAILABLE, "SocketStatus drifted");
static_assert(static_cast<I32>(SocketStatus::ANOTHER_THREAD_OPENING) == SOCK_ANOTHER_THREAD_OPENING,
              "SocketStatus drifted");
static_assert(static_cast<I32>(SocketStatus::AUTO_CONNECT_DISABLED) == SOCK_AUTO_CONNECT_DISABLED,
              "SocketStatus drifted");
static_assert(static_cast<I32>(SocketStatus::INVALID_CALL) == SOCK_INVALID_CALL, "SocketStatus drifted");

namespace {

//! Whole seconds in the microseconds field belong in the seconds field
void normalize(SendTimeout& timeout) {
    timeout.set_seconds(timeout.get_seconds() + (timeout.get_microseconds() / 1000000));
    timeout.set_microseconds(timeout.get_microseconds() % 1000000);
}

}  // namespace

void UnifiedByteStreamDriver::formatAddress(const IpEndpoint& endpoint, Fw::String& address) {
    const IpEndpoint::Type_of_address& octets = endpoint.get_address();
    (void)address.format("%u.%u.%u.%u", octets[0], octets[1], octets[2], octets[3]);
}

void UnifiedByteStreamDriver::formatEndpoint(const IpEndpoint& endpoint, const U16 port, Fw::String& text) {
    Fw::String address;
    UnifiedByteStreamDriver::formatAddress(endpoint, address);
    (void)text.format("%s:%hu", address.toChar(), port);
}

bool UnifiedByteStreamDriver::hasAddress(const IpEndpoint& endpoint) {
    const IpEndpoint::Type_of_address& octets = endpoint.get_address();
    return (octets[0] | octets[1] | octets[2] | octets[3]) != 0;
}

bool UnifiedByteStreamDriver::remoteSpecified(const ParameterSet& params) {
    return UnifiedByteStreamDriver::hasAddress(params.remoteEndpoint) || (params.remoteEndpoint.get_port() != 0);
}

bool UnifiedByteStreamDriver::remoteComplete(const ParameterSet& params) {
    return UnifiedByteStreamDriver::hasAddress(params.remoteEndpoint) && (params.remoteEndpoint.get_port() != 0);
}

ByteStreamTransport UnifiedByteStreamDriver::reject(const ParameterSet& params,
                                                    const ByteStreamConfigError error) const {
    this->log_WARNING_HI_UnsupportedConfiguration(params.transport, error);
    return ByteStreamTransport::NONE;
}

ByteStreamTransport UnifiedByteStreamDriver::configure() {
    Os::ScopeLock lock(this->m_config.lock);
    return this->applyConfigurationLocked();
}

UnifiedByteStreamDriver::ParameterSet UnifiedByteStreamDriver::snapshotParameters() {
    Fw::ParamValid valid = Fw::ParamValid::UNINIT;
    ParameterSet params;
    params.transport = this->paramGet_TRANSPORT(valid);
    params.localEndpoint = this->paramGet_LOCAL_ENDPOINT(valid);
    params.remoteEndpoint = this->paramGet_REMOTE_ENDPOINT(valid);
    params.serial = this->paramGet_SERIAL_CONFIG(valid);
    params.recvBufferSize = this->paramGet_RECV_BUFFER_SIZE(valid);
    params.sendTimeout = this->paramGet_SEND_TIMEOUT(valid);
    normalize(params.sendTimeout);
    return params;
}

bool UnifiedByteStreamDriver::parametersValid(const ParameterSet& params, ByteStreamConfigError& error) const {
    if (params.recvBufferSize == 0) {
        error = ByteStreamConfigError::INVALID_BUFFER_SIZE;
        return false;
    }
    switch (params.transport.e) {
        case ByteStreamTransport::TCP_CLIENT:
            if (not UnifiedByteStreamDriver::remoteSpecified(params)) {
                error = ByteStreamConfigError::MISSING_REMOTE_ENDPOINT;
                return false;
            }
            // A client must reach its destination; UDP need only reach one it was given.
            // fall through
        case ByteStreamTransport::UDP:
            if (UnifiedByteStreamDriver::remoteSpecified(params) &&
                (not UnifiedByteStreamDriver::remoteComplete(params))) {
                error = ByteStreamConfigError::INCOMPLETE_REMOTE_ENDPOINT;
                return false;
            }
            break;
        case ByteStreamTransport::SERIAL:
            if (params.serial.get_device().length() == 0) {
                error = ByteStreamConfigError::NO_SERIAL_DEVICE;
                return false;
            }
            if (not SerialStream::isBaudRateSupported(params.serial.get_baudRate())) {
                error = ByteStreamConfigError::UNSUPPORTED_BAUD_RATE;
                return false;
            }
            break;
        case ByteStreamTransport::NONE:
        case ByteStreamTransport::TCP_SERVER:
            break;
        default:
            error = ByteStreamConfigError::TRANSPORT_REJECTED_SETTINGS;
            return false;
    }
    return true;
}

ByteStreamTransport UnifiedByteStreamDriver::applyConfigurationLocked() {
    this->m_config.reconfigurePending = false;
    this->m_config.resolved = true;
    const ParameterSet params = this->snapshotParameters();
    this->m_config.params = params;

    // Ahead of every configure call, so a rejection cannot leave a live socket carrying settings
    // this driver refused.
    ByteStreamConfigError error = ByteStreamConfigError::TRANSPORT_REJECTED_SETTINGS;
    if (not this->parametersValid(params, error)) {
        this->m_config.transport = this->reject(params, error);
        this->reportConfiguration();
        return this->m_config.transport;
    }
    this->m_config.allocationSize = params.recvBufferSize;

    switch (params.transport.e) {
        case ByteStreamTransport::NONE:
            this->m_config.transport = ByteStreamTransport::NONE;
            break;
        case ByteStreamTransport::TCP_CLIENT:
            this->m_config.transport = this->configureTcpClient(params);
            break;
        case ByteStreamTransport::TCP_SERVER:
            this->m_config.transport = this->configureTcpServer(params);
            break;
        case ByteStreamTransport::UDP:
            this->m_config.transport = this->configureUdp(params);
            break;
        case ByteStreamTransport::SERIAL:
            this->m_config.transport = this->configureSerial(params);
            break;
        default:
            this->m_config.transport = this->reject(params, ByteStreamConfigError::TRANSPORT_REJECTED_SETTINGS);
            break;
    }

    this->buildEndpoint(params);
    if (this->m_config.transport != ByteStreamTransport::NONE) {
        this->log_ACTIVITY_HI_ConfigurationApplied(this->m_config.transport, this->m_config.endpoint);
    }
    this->reportConfiguration();
    return this->m_config.transport;
}

ByteStreamTransport UnifiedByteStreamDriver::configureTcpClient(const ParameterSet& params) {
    Fw::String address;
    UnifiedByteStreamDriver::formatAddress(params.remoteEndpoint, address);
    const SocketIpStatus status =
        this->m_tcpClient.configure(address.toChar(), params.remoteEndpoint.get_port(),
                                    params.sendTimeout.get_seconds(), params.sendTimeout.get_microseconds());
    if (status != SOCK_SUCCESS) {
        return this->reject(params, ByteStreamConfigError::TRANSPORT_REJECTED_SETTINGS);
    }
    return ByteStreamTransport::TCP_CLIENT;
}

ByteStreamTransport UnifiedByteStreamDriver::configureTcpServer(const ParameterSet& params) {
    Fw::String address;
    UnifiedByteStreamDriver::formatAddress(params.localEndpoint, address);
    const SocketIpStatus status =
        this->m_tcpServer.configure(address.toChar(), params.localEndpoint.get_port(), params.sendTimeout.get_seconds(),
                                    params.sendTimeout.get_microseconds());
    if (status != SOCK_SUCCESS) {
        return this->reject(params, ByteStreamConfigError::TRANSPORT_REJECTED_SETTINGS);
    }
    return ByteStreamTransport::TCP_SERVER;
}

ByteStreamTransport UnifiedByteStreamDriver::configureUdp(const ParameterSet& params) {
    Fw::String address;
    UnifiedByteStreamDriver::formatAddress(params.localEndpoint, address);
    SocketIpStatus status = this->m_udp.configureRecv(address.toChar(), params.localEndpoint.get_port());
    if (status != SOCK_SUCCESS) {
        return this->reject(params, ByteStreamConfigError::TRANSPORT_REJECTED_SETTINGS);
    }

    // Without a destination the socket replies to whoever sent the last datagram
    if (UnifiedByteStreamDriver::remoteSpecified(params)) {
        UnifiedByteStreamDriver::formatAddress(params.remoteEndpoint, address);
        status = this->m_udp.configureSend(address.toChar(), params.remoteEndpoint.get_port(),
                                           params.sendTimeout.get_seconds(), params.sendTimeout.get_microseconds());
        if (status != SOCK_SUCCESS) {
            return this->reject(params, ByteStreamConfigError::TRANSPORT_REJECTED_SETTINGS);
        }
    }
    return ByteStreamTransport::UDP;
}

ByteStreamTransport UnifiedByteStreamDriver::configureSerial(const ParameterSet& params) {
    const SocketIpStatus status = this->m_serial.configureSerial(
        params.serial.get_device().toChar(), params.serial.get_baudRate(), params.serial.get_parity(),
        params.serial.get_flowControl(), params.serial.get_readTimeout());
    if (status != SOCK_SUCCESS) {
        return this->reject(params, ByteStreamConfigError::TRANSPORT_REJECTED_SETTINGS);
    }
    return ByteStreamTransport::SERIAL;
}

void UnifiedByteStreamDriver::buildEndpoint(const ParameterSet& params) {
    if (this->m_config.transport == ByteStreamTransport::SERIAL) {
        (void)this->m_config.endpoint.format("%s @ %u", params.serial.get_device().toChar(),
                                             static_cast<unsigned int>(params.serial.get_baudRate()));
        return;
    }
    // A wildcard local port is only resolved when the transport opens, so prefer the port
    // actually bound over the requested one
    const U16 bound = this->boundPort();
    const U16 localPort = (bound != 0) ? bound : params.localEndpoint.get_port();
    const bool binds = (this->m_config.transport == ByteStreamTransport::TCP_SERVER) ||
                       (this->m_config.transport == ByteStreamTransport::UDP);
    const bool sends =
        (this->m_config.transport == ByteStreamTransport::TCP_CLIENT) ||
        ((this->m_config.transport == ByteStreamTransport::UDP) && UnifiedByteStreamDriver::remoteSpecified(params));

    this->m_config.endpoint = "";
    Fw::String endpoint;
    if (binds) {
        UnifiedByteStreamDriver::formatEndpoint(params.localEndpoint, localPort, endpoint);
        (void)this->m_config.endpoint.format("bind %s", endpoint.toChar());
    }
    if (sends) {
        UnifiedByteStreamDriver::formatEndpoint(params.remoteEndpoint, params.remoteEndpoint.get_port(), endpoint);
        const Fw::String bind = this->m_config.endpoint;
        (void)this->m_config.endpoint.format("%s%ssend %s", bind.toChar(), binds ? " " : "", endpoint.toChar());
    }
}

bool UnifiedByteStreamDriver::parameterValid(const Fw::ParamValid valid) {
    return (valid == Fw::ParamValid::VALID) || (valid == Fw::ParamValid::DEFAULT);
}

void UnifiedByteStreamDriver::reportConfiguration() {
    this->tlmWrite_ActiveTransport(this->m_config.transport);
    this->tlmWrite_ActiveEndpoint(this->m_config.endpoint);
    this->tlmWrite_LocalPort(this->boundPort());
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
    Fw::ParamValid valid = Fw::ParamValid::UNINIT;
    switch (id) {
        case PARAMID_TRANSPORT: {
            const ByteStreamTransport value = this->paramGet_TRANSPORT(valid);
            FW_ASSERT(UnifiedByteStreamDriver::parameterValid(valid), static_cast<FwAssertArgType>(valid.e));
            this->tlmWrite_TRANSPORT(value);
            break;
        }
        case PARAMID_LOCAL_ENDPOINT: {
            const IpEndpoint value = this->paramGet_LOCAL_ENDPOINT(valid);
            FW_ASSERT(UnifiedByteStreamDriver::parameterValid(valid), static_cast<FwAssertArgType>(valid.e));
            this->tlmWrite_LOCAL_ENDPOINT(value);
            break;
        }
        case PARAMID_REMOTE_ENDPOINT: {
            const IpEndpoint value = this->paramGet_REMOTE_ENDPOINT(valid);
            FW_ASSERT(UnifiedByteStreamDriver::parameterValid(valid), static_cast<FwAssertArgType>(valid.e));
            this->tlmWrite_REMOTE_ENDPOINT(value);
            break;
        }
        case PARAMID_SERIAL_CONFIG: {
            const SerialConfig value = this->paramGet_SERIAL_CONFIG(valid);
            FW_ASSERT(UnifiedByteStreamDriver::parameterValid(valid), static_cast<FwAssertArgType>(valid.e));
            this->tlmWrite_SERIAL_CONFIG(value);
            break;
        }
        case PARAMID_RECV_BUFFER_SIZE: {
            const FwSizeType value = this->paramGet_RECV_BUFFER_SIZE(valid);
            FW_ASSERT(UnifiedByteStreamDriver::parameterValid(valid), static_cast<FwAssertArgType>(valid.e));
            this->tlmWrite_RECV_BUFFER_SIZE(value);
            break;
        }
        case PARAMID_SEND_TIMEOUT: {
            const SendTimeout value = this->paramGet_SEND_TIMEOUT(valid);
            FW_ASSERT(UnifiedByteStreamDriver::parameterValid(valid), static_cast<FwAssertArgType>(valid.e));
            this->tlmWrite_SEND_TIMEOUT(value);
            break;
        }
        default:
            FW_ASSERT(0, static_cast<FwAssertArgType>(id));
            break;
    }

    // Every parameter feeds the same one resolution, so which of them changed does not
    // matter below. All that is left to decide is when that resolution can happen.
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
            (void)this->applyConfigurationLocked();
            return;
        }
        // The teardown below reaches getSocketHandler on this thread, where applying the
        // change would be the very cross-thread write this hand-off exists to avoid
        this->m_config.suppressApply = true;
        live = this->m_config.transport;
    }
    // Drop the read task out of the helper's loop and back into ours, which is where a
    // listening socket is brought up and released. Without this the loop keeps running on
    // the transport it entered with, and a change into TCP_SERVER would never listen.
    this->setAutomaticOpen(false);
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
        const ByteStreamTransport transport = (this->m_config.reconfigurePending || (not this->m_config.resolved))
                                                  ? this->applyConfigurationLocked()
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

void UnifiedByteStreamDriver::applyPendingLocked() {
    // The read and reconnect tasks come through here before every open and every receive,
    // which is where a configuration staged by parameterUpdated is safe to apply
    if (this->m_config.reconfigurePending && (not this->m_config.suppressApply)) {
        const ByteStreamTransport previous = this->m_config.transport;
        if (this->applyConfigurationLocked() == ByteStreamTransport::NONE) {
            // A change that resolves to nothing must not take a working link down with it
            this->m_config.transport = previous;
        }
    }
}

bool UnifiedByteStreamDriver::settleConfiguration() {
    Os::ScopeLock lock(this->m_config.lock);
    this->applyPendingLocked();
    // Still pending means a teardown is running on another thread and the values are not
    // final yet. Binding a listening socket to them now would bind the previous ones.
    return not this->m_config.reconfigurePending;
}

ByteStreamTransport UnifiedByteStreamDriver::getTransport() const {
    Os::ScopeLock lock(this->m_config.lock);
    return this->m_config.transport;
}

U16 UnifiedByteStreamDriver::getLocalPort() {
    Os::ScopeLock lock(this->m_config.lock);
    return this->boundPort();
}

U16 UnifiedByteStreamDriver::boundPort() {
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
    this->applyPendingLocked();
    switch (this->m_config.transport.e) {
        case ByteStreamTransport::TCP_CLIENT:
            return this->m_tcpClient;
        case ByteStreamTransport::TCP_SERVER:
            return this->m_tcpServer;
        case ByteStreamTransport::UDP:
            return this->m_udp;
        case ByteStreamTransport::SERIAL:
            return this->m_serial;
        default:
            FW_ASSERT(0, static_cast<FwAssertArgType>(this->m_config.transport.e));
            return this->m_udp;
    }
}

Fw::Buffer UnifiedByteStreamDriver::getBuffer() {
    FwSizeType size = 0;
    {
        Os::ScopeLock lock(this->m_config.lock);
        size = this->m_config.allocationSize;
    }
    const Fw::Buffer buffer = this->allocate_out(0, size);
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
        this->tlmWrite_BytesRecv(this->m_bytesReceived += buffer.getSize());
    } else if (status == SOCK_NO_DATA_AVAILABLE) {
        recvStatus = ByteStreamStatus::RECV_NO_DATA;
    } else {
        recvStatus = ByteStreamStatus::OTHER_ERROR;
        this->log_WARNING_LO_ReceiveError(SocketStatus(static_cast<SocketStatus::T>(status)));
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
        this->buildEndpoint(this->m_config.params);  // an ephemeral port only has a value now
        transport = this->m_config.transport;
        endpoint = this->m_config.endpoint;
        localPort = this->boundPort();
    }
    this->log_ACTIVITY_HI_PortOpened(transport, endpoint);
    this->tlmWrite_LocalPort(localPort);
    this->tlmWrite_Connected(true);
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

void UnifiedByteStreamDriver::releaseListener() {
    Os::ScopeLock scopedLock(this->m_lock);
    if (this->m_descriptor.serverFd != -1) {
        this->m_tcpServer.terminate(this->m_descriptor);
        this->m_descriptor.serverFd = -1;
    }
}

void UnifiedByteStreamDriver::readLoop() {
    // One pass of this loop is one transport. The helper's loop runs until the transport is
    // taken away from underneath it, which is how a change of transport gets back here: a
    // listening socket belongs to the transport that wanted it, so it is brought up and
    // released around the helper's loop rather than around the task.
    while (this->running()) {
        // @non-terminating@: bounded by the stop request
        while (this->running() && (not this->settleConfiguration())) {
            (void)Os::Task::delay(Fw::TimeInterval(0, 1000));
        }
        this->setAutomaticOpen(true);
        if (this->getTransport() == ByteStreamTransport::TCP_SERVER) {
            Drv::SocketIpStatus status = Drv::SocketIpStatus::SOCK_NOT_STARTED;
            // Keep trying to listen until it works, a stop is requested, or reopen is disabled
            // @non-terminating@: retry loop bounded by stop request
            do {
                status = this->startupServer();
                if (status != SOCK_SUCCESS) {
                    U16 listenPort = 0;
                    {
                        Os::ScopeLock lock(this->m_config.lock);
                        listenPort = this->m_tcpServer.getListenPort();
                    }
                    Fw::Logger::log("[WARNING] Failed to listen on port %hu with status %d\n", listenPort, status);
                    (void)Os::Task::delay(SOCKET_RETRY_INTERVAL);
                }
            } while (this->running() && (status != SOCK_SUCCESS) && this->getAutomaticOpen());

            if (status != SOCK_SUCCESS) {
                this->releaseListener();
                break;
            }
        }
        SocketComponentHelper::readLoop();
        this->releaseListener();
    }
    this->tlmWrite_Connected(false);
}

// ----------------------------------------------------------------------
// Handler implementations for user-defined typed input ports
// ----------------------------------------------------------------------

Drv::ByteStreamStatus UnifiedByteStreamDriver::send_handler(const FwIndexType portNum, Fw::Buffer& fwBuffer) {
    if (this->getTransport() == ByteStreamTransport::NONE) {
        this->log_WARNING_HI_SendWithoutTransport();
        return ByteStreamStatus::OTHER_ERROR;
    }
    const FwSizeType size = fwBuffer.getSize();
    const Drv::SocketIpStatus status = this->send(fwBuffer.getData(), size);
    Drv::ByteStreamStatus returnStatus = ByteStreamStatus::OTHER_ERROR;
    switch (status) {
        case SOCK_SUCCESS:
            this->tlmWrite_BytesSent(this->m_bytesSent += size);
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
        this->log_WARNING_LO_SendError(SocketStatus(static_cast<SocketStatus::T>(status)));
    }
    return returnStatus;
}

void UnifiedByteStreamDriver::recvReturnIn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) {
    this->deallocate_out(0, fwBuffer);
}

}  // end namespace Drv
