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
 * optional local address/port pair, an optional remote address/port pair, and an optional
 * serial device, and the pairs that are supplied decide the direction of the link:
 *
 * | TRANSPORT | local | remote | resolved mode                                    |
 * |-----------|-------|--------|--------------------------------------------------|
 * | TCP       | set   | unset  | TCP server listening on the local endpoint       |
 * | TCP       | unset | set    | TCP client connecting to the remote endpoint     |
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
 * All four transports are held as members. They are small, and holding them avoids
 * dynamic allocation while letting `getSocketHandler` hand the shared
 * Drv::SocketComponentHelper machinery whichever transport the parameters selected.
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
     * \return the resolved mode, DISABLED when the parameters were rejected
     */
    ByteStreamDriverMode configure();

    /**
     * \brief start the driver's read and reconnect tasks
     *
     * Configures from parameters when that has not happened yet. Starts no tasks when the
     * configuration resolved to DISABLED, so a rejected configuration costs nothing at
     * runtime beyond the warning it already emitted.
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
     * Safe to call when the driver was never started or resolved to DISABLED.
     */
    void stop();

    /**
     * \brief wait for the driver's tasks to finish
     *
     * \return status of the join, OP_OK when the driver was never started
     */
    Os::Task::Status join();

    /**
     * \brief get the mode resolved from the parameters
     *
     * \return resolved mode, DISABLED until `configure` has run or when it was rejected
     */
    ByteStreamDriverMode getMode() const;

    /**
     * \brief get the local port the driver is actually using
     *
     * Most useful when LOCAL_PORT was 0 and the port was assigned when the transport
     * opened. Returns 0 for modes that do not bind a local port, and before the transport
     * has been opened.
     *
     * \return local port in use
     */
    U16 getLocalPort();

    /**
     * \brief check whether an address is a dotted-quad IPv4 address
     *
     * The IP transports do not resolve host names, so an address that is not a dotted quad
     * would only fail once the socket was opened. Checking up front turns that into a
     * configuration warning. Leading zeros are rejected, matching inet_pton.
     *
     * \param address: NUL-terminated address to check
     * \return true when the address is a dotted-quad IPv4 address
     */
    static bool isDottedQuadIpv4(const char* const address);

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

    //! \brief drive the read task, adapted to the resolved mode
    //!
    //! A TCP server has to bring up its listening socket first and tear it down at the
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
    //! should retry, OTHER_ERROR when the driver is disabled or the failure is not
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
        ByteStreamTransport transport = ByteStreamTransport::NONE;
        Fw::ParamString localAddress;
        U16 localPort = 0;
        Fw::ParamString remoteAddress;
        U16 remotePort = 0;
        Fw::ParamString serialDevice;
        SerialBaudRate baudRate = SerialBaudRate::BAUD_115200;
        SerialParity parity = SerialParity::PARITY_NONE;
        SerialFlowControl flowControl = SerialFlowControl::FLOW_NONE;
        FwSizeType recvBufferSize = 0;
        U32 sendTimeoutSeconds = 0;
        U32 sendTimeoutMicroseconds = 0;

        //! \brief whether a local address/port pair was supplied
        bool hasLocal() const { return this->localAddress.length() > 0; }
        //! \brief whether a remote address/port pair was supplied
        bool hasRemote() const { return this->remoteAddress.length() > 0; }
        //! \brief whether a serial device was supplied
        bool hasSerial() const { return this->serialDevice.length() > 0; }
    };

    //! \brief read every parameter into a single snapshot
    Parameters readParameters();

    //! \brief configure a TCP client or server from the parameters
    //! \return resolved mode, DISABLED when the combination was rejected
    ByteStreamDriverMode configureTcp(const Parameters& parameters);

    //! \brief configure UDP from the parameters
    //! \return resolved mode, DISABLED when the combination was rejected
    ByteStreamDriverMode configureUdp(const Parameters& parameters);

    //! \brief configure the serial device from the parameters
    //! \return resolved mode, DISABLED when the combination was rejected
    ByteStreamDriverMode configureSerial(const Parameters& parameters);

    //! \brief report a rejected configuration
    //! \return DISABLED, so that callers can return this directly
    ByteStreamDriverMode reject(const ByteStreamTransport transport, const ByteStreamConfigError error) const;

    //! \brief hold the transport open without reading, for a send-only configuration
    void holdOpenLoop();

    //! \brief bring up the TCP server's listening socket
    SocketIpStatus startupServer();

    //! \brief tear down the TCP server's listening socket
    void terminateServer();

    //! \brief build the endpoint description from the resolved mode and parameters
    //!
    //! A local port of 0 asks the system for an ephemeral port, which is only known once
    //! the transport has opened, so this prefers the port actually in use.
    void buildEndpoint();

    // ----------------------------------------------------------------------
    // Member variables
    // ----------------------------------------------------------------------

    TcpClientSocket m_tcpClient;  //!< TCP client transport
    TcpServerSocket m_tcpServer;  //!< TCP server transport
    UdpSocket m_udp;              //!< UDP transport
    SerialStream m_serial;        //!< Serial transport

    Parameters m_parameters;      //!< Snapshot of the parameters the configuration came from
    ByteStreamDriverMode m_mode;  //!< Mode resolved from the parameters
    Fw::String m_endpoint;        //!< Human-readable description of the resolved endpoint
    FwSizeType m_allocationSize;  //!< Size of the buffers allocated for receiving
    bool m_receiveEnabled;        //!< Whether the resolved mode has a receive direction
    bool m_configured;            //!< Whether configure() has already run
    bool m_started;               //!< Whether the tasks have been started

    std::atomic<FwSizeType> m_bytesSent;      //!< Bytes handed to the transport
    std::atomic<FwSizeType> m_bytesReceived;  //!< Bytes received from the transport
};

}  // namespace Drv

#endif  // DRV_UNIFIEDBYTESTREAMDRIVER_HPP
