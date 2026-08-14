// ======================================================================
// \title  UnifiedByteStreamDriver.hpp
// \author fprime
// \brief  hpp file for UnifiedByteStreamDriver component implementation class
//
// \copyright
// Copyright 2009-2025, by the California Institute of Technology.
// ALL RIGHTS RESERVED.  United States Government Sponsorship
// acknowledged.
//
// ======================================================================

#ifndef DRV_UNIFIEDBYTESTREAMDRIVER_HPP
#define DRV_UNIFIEDBYTESTREAMDRIVER_HPP

#include <Drv/Ip/IpSocket.hpp>
#include <Drv/Ip/SocketComponentHelper.hpp>
#include <Drv/Ip/TcpClientSocket.hpp>
#include <Drv/Ip/TcpServerSocket.hpp>
#include <Drv/Ip/UdpSocket.hpp>
#include <Drv/UnifiedByteStreamDriver/SerialStream.hpp>
#include <Drv/UnifiedByteStreamDriver/UnifiedByteStreamDriverComponentAc.hpp>
#include <Fw/Types/String.hpp>
#include <Os/Task.hpp>
#include <atomic>

namespace Drv {

/**
 * \brief a byte stream driver whose transport is chosen by parameters
 *
 * This component covers what Drv::TcpClient, Drv::TcpServer, Drv::Udp and
 * Drv::LinuxUartDriver each cover individually, and selects between them from its
 * parameters rather than from the topology. Deployments that need to move a link from,
 * say, a UDP socket to a serial line then do so by changing parameter values instead of
 * rebuilding a topology.
 *
 * The transport is chosen with the TRANSPORT parameter. The endpoints are given as an
 * optional local Drv::IpEndpoint, an optional remote Drv::IpEndpoint, and an optional
 * serial device. An endpoint is unset when its port is zero, and which endpoints are set
 * decides the direction of the link:
 *
 * | TRANSPORT | local | remote | behavior                                         |
 * |-----------|-------|--------|--------------------------------------------------|
 * | TCP       | set   | unset  | listens on the local endpoint                    |
 * | TCP       | unset | set    | connects to the remote endpoint                  |
 * | TCP       | set   | set    | rejected: no transport binds and connects        |
 * | UDP       | set   | unset  | bound locally, replies to the last sender        |
 * | UDP       | unset | set    | send-only to the remote endpoint                 |
 * | UDP       | set   | set    | bound locally, sending to the remote endpoint    |
 * | SERIAL    | -     | -      | serial device; IP parameters warn and are unused |
 *
 * A combination that cannot be served emits UnsupportedConfiguration and leaves the driver
 * disabled, rather than bringing up a link that only works in one direction. Parameters
 * that do not apply to the selected transport emit IgnoredConfiguration.
 *
 * Configuration is resolved exactly once, when parameters are loaded (or on the first
 * `start` for deployments without a parameter database). Changing a parameter afterwards
 * emits ConfigurationChangeDeferred: the new value applies at the next restart, because
 * tearing down and rebuilding a live transport underneath the read task is not safe.
 *
 * The transports themselves are the ones the rest of the framework already uses:
 * Drv::TcpClientSocket, Drv::TcpServerSocket and Drv::UdpSocket, driven by the read and
 * reconnect tasks of Drv::SocketComponentHelper. Drv::SerialStream presents the serial
 * device through the same Drv::IpSocket interface, so one read loop serves all four. All
 * four are held as members: they are small, and holding them avoids dynamic allocation
 * while letting `getSocketHandler` hand the helper whichever one the parameters selected.
 */
class UnifiedByteStreamDriver final : public UnifiedByteStreamDriverComponentBase, public SocketComponentHelper {
    friend class UnifiedByteStreamDriverTester;

  public:
    // ----------------------------------------------------------------------
    // Construction, initialization, and destruction
    // ----------------------------------------------------------------------

    //! \brief construct the UnifiedByteStreamDriver component
    //! \param compName: name of this component
    explicit UnifiedByteStreamDriver(const char* const compName);

    //! \brief destroy the component
    ~UnifiedByteStreamDriver() override;

    // ----------------------------------------------------------------------
    // Public interface
    // ----------------------------------------------------------------------

    /**
     * \brief resolve the parameters into a transport configuration
     *
     * Reads every parameter, checks the combination, emits the matching configuration
     * events, and configures the selected transport without opening it. This is called
     * automatically when parameters are loaded and again on the first `start`, so most
     * deployments never call it directly.
     *
     * Only the first call configures the driver. Later calls report
     * ConfigurationChangeDeferred and leave the existing configuration alone.
     *
     * \return the transport in use, NONE when the parameters were rejected
     */
    ByteStreamTransport configure();

    /**
     * \brief start the driver's read and reconnect tasks
     *
     * Configures from parameters when that has not happened yet. Starts no tasks when the
     * configuration was rejected, so a rejected configuration costs nothing at runtime
     * beyond the warning it already emitted.
     *
     * \param priority: priority of the read task. See: Os::Task::start
     * \param stack: stack size of the read task. See: Os::Task::start
     * \param cpuAffinity: cpu affinity of the read task. See: Os::Task::start
     */
    void start(const FwTaskPriorityType priority = Os::Task::TASK_PRIORITY_DEFAULT,
               const Os::Task::ParamType stack = Os::Task::TASK_DEFAULT,
               const Os::Task::ParamType cpuAffinity = Os::Task::TASK_DEFAULT);

    /**
     * \brief stop the driver's tasks and close the transport
     *
     * Safe to call when the driver was never started or was never configured.
     */
    void stop();

    /**
     * \brief wait for the driver's tasks to finish
     *
     * \return status of the join, OP_OK when the driver was never started
     */
    Os::Task::Status join();

    /**
     * \brief get the transport resolved from the parameters
     *
     * \return transport in use, NONE until `configure` has run or when it was rejected
     */
    ByteStreamTransport getTransport() const;

    /**
     * \brief get the local port the transport is actually bound to
     *
     * Returns 0 for configurations that bind no local port, and before the transport has
     * been opened.
     *
     * \return local port in use
     */
    U16 getLocalPort();

  protected:
    // ----------------------------------------------------------------------
    // Implementations for socket read task virtual methods
    // ----------------------------------------------------------------------

    //! \brief return the transport selected by the parameters
    IpSocket& getSocketHandler() override;

    //! \brief allocate a buffer for the read task to fill
    Fw::Buffer getBuffer() override;

    //! \brief forward a filled buffer out of the driver
    void sendBuffer(Fw::Buffer buffer, SocketIpStatus status) override;

    //! \brief called when the transport has opened
    void connected() override;

    //! \brief drive the read task, adapted to the resolved configuration
    //!
    //! A TCP listener has to bring up its listening socket first and tear it down at the
    //! end. A send-only transport has no receive direction to read, so it holds the
    //! transport open instead of reading from it. Everything else uses the standard loop.
    void readLoop() override;

    // ----------------------------------------------------------------------
    // Parameter hooks
    // ----------------------------------------------------------------------

    //! \brief resolve the configuration once the parameters are available
    void parametersLoaded() override;

    //! \brief report that a parameter change only takes effect on the next restart
    void parameterUpdated(FwPrmIdType id) override;

  private:
    // ----------------------------------------------------------------------
    // Handler implementations for user-defined typed input ports
    // ----------------------------------------------------------------------

    //! \brief send data out of the driver
    //!
    //! Returns SEND_RETRY when the transport is momentarily unavailable and the caller
    //! should retry, OTHER_ERROR when the driver is unconfigured or the failure is not
    //! recoverable, and OP_OK once the data has been handed to the transport.
    Drv::ByteStreamStatus send_handler(const FwIndexType portNum, Fw::Buffer& fwBuffer) override;

    //! \brief take back ownership of a buffer sent out on the recv port
    void recvReturnIn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) override;

    //! \brief emit telemetry
    void run_handler(FwIndexType portNum, U32 context) override;

    // ----------------------------------------------------------------------
    // Configuration helpers
    // ----------------------------------------------------------------------

    //! \brief parameter values read as a set, so that validation sees one consistent view
    struct Parameters {
        ByteStreamTransport transport;
        IpEndpoint localEndpoint;
        IpEndpoint remoteEndpoint;
        Fw::ParamString serialDevice;
        SerialBaudRate baudRate;
        SerialParity parity;
        SerialFlowControl flowControl;
        FwSizeType recvBufferSize = 0;
        U32 sendTimeoutSeconds = 0;
        U32 sendTimeoutMicroseconds = 0;

        //! \brief whether a local endpoint was supplied
        bool hasLocal() const { return this->localEndpoint.get_port() != 0; }
        //! \brief whether a remote endpoint was supplied
        bool hasRemote() const { return this->remoteEndpoint.get_port() != 0; }
        //! \brief whether a serial device was supplied
        bool hasSerial() const { return this->serialDevice.length() > 0; }
    };

    //! \brief read every parameter into a single snapshot
    Parameters readParameters();

    //! \brief configure TCP from the parameters
    //! \return transport in use, NONE when the combination was rejected
    ByteStreamTransport configureTcp(const Parameters& parameters);

    //! \brief configure UDP from the parameters
    //! \return transport in use, NONE when the combination was rejected
    ByteStreamTransport configureUdp(const Parameters& parameters);

    //! \brief configure the serial device from the parameters
    //! \return transport in use, NONE when the combination was rejected
    ByteStreamTransport configureSerial(const Parameters& parameters);

    //! \brief report a rejected configuration
    //! \return NONE, so that callers can return this directly
    ByteStreamTransport reject(const ByteStreamTransport transport, const ByteStreamConfigError error) const;

    //! \brief render an endpoint's octets as the dotted-quad string the sockets take
    static void formatAddress(const IpEndpoint& endpoint, Fw::String& address);

    //! \brief build the endpoint description reported in events
    void buildEndpoint(const Parameters& parameters);

    //! \brief hold the transport open without reading, for a send-only configuration
    void holdOpenLoop();

    //! \brief bring up the TCP listening socket
    SocketIpStatus startupServer();

    //! \brief tear down the TCP listening socket
    void terminateServer();

    // ----------------------------------------------------------------------
    // Member variables
    // ----------------------------------------------------------------------

    TcpClientSocket m_tcpClient;  //!< TCP connecting transport
    TcpServerSocket m_tcpServer;  //!< TCP listening transport
    UdpSocket m_udp;              //!< UDP transport
    SerialStream m_serial;        //!< Serial transport

    //! Transport resolved from the parameters, NONE until configured or when rejected
    ByteStreamTransport m_transport = ByteStreamTransport::NONE;
    //! Whether a TCP transport listens rather than connects
    bool m_listening = false;
    //! Human-readable description of the resolved endpoint
    Fw::String m_endpoint;
    //! Size of the buffers allocated for receiving
    FwSizeType m_allocationSize = 0;
    //! Whether the resolved configuration has a receive direction
    bool m_receiveEnabled = false;
    //! Whether configure() has already run
    bool m_configured = false;
    //! Whether the tasks have been started
    bool m_started = false;

    std::atomic<FwSizeType> m_bytesSent{0};      //!< Bytes handed to the transport
    std::atomic<FwSizeType> m_bytesReceived{0};  //!< Bytes received from the transport
};

}  // namespace Drv

#endif  // DRV_UNIFIEDBYTESTREAMDRIVER_HPP
