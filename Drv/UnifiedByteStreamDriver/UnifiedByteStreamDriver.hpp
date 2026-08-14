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
#include <Drv/UnifiedByteStreamDriver/UnifiedByteStreamDriverComponentAc.hpp>
#include <Fw/Types/String.hpp>
#include <Os/Task.hpp>
#include <atomic>
#include "SerialStream.hpp"

namespace Drv {

/**
 * \brief a byte stream driver whose transport is chosen by parameters
 *
 * Covers what Drv::TcpClient, Drv::TcpServer, Drv::Udp and Drv::LinuxUartDriver cover
 * individually. TRANSPORT picks the transport, the remote endpoint picks the direction, and
 * a serial device names the line.
 *
 * Zero carries the meaning the transports already give it, which differs by the endpoint's
 * role. The local endpoint is bound, so 0.0.0.0 binds every interface and a zero port takes
 * an ephemeral one; an entirely zero local endpoint is therefore a wildcard bind, not a
 * missing one. The remote endpoint is a destination, so an entirely zero one is no
 * destination at all, and one that is only partly zero is rejected.
 *
 * | TRANSPORT | remote        | behavior                                            |
 * |-----------|---------------|-----------------------------------------------------|
 * | TCP       | reachable     | connects to it; a local endpoint asked for conflicts |
 * | TCP       | none          | listens on the local endpoint                       |
 * | UDP       | reachable     | binds the local endpoint and sends to the remote    |
 * | UDP       | none          | binds the local endpoint, replies to the last sender |
 * | SERIAL    | -             | serial device; IP parameters warn, unused           |
 *
 * A combination that cannot be served emits UnsupportedConfiguration and leaves the driver
 * unconfigured rather than half-configured. Parameters that do not apply to the selected
 * transport emit IgnoredConfiguration.
 *
 * Configuration resolves once, when parameters load or on the first `start`. A later
 * parameter change emits ConfigurationChangeDeferred and applies at the next restart:
 * rebuilding a transport underneath a live read task is not safe.
 *
 * The transports are the framework's own: Drv::TcpClientSocket, Drv::TcpServerSocket and
 * Drv::UdpSocket, driven by Drv::SocketComponentHelper. Drv::SerialStream presents the
 * serial device through the same Drv::IpSocket interface, so one read loop serves all
 * four. All four are held as members to avoid dynamic allocation.
 */
class UnifiedByteStreamDriver final : public UnifiedByteStreamDriverComponentBase, public SocketComponentHelper {
    friend class UnifiedByteStreamDriverTester;

  public:
    explicit UnifiedByteStreamDriver(const char* const compName);
    ~UnifiedByteStreamDriver() override;

    //! \brief resolve the parameters into a transport configuration, without opening it
    //!
    //! Called automatically when parameters load and on the first `start`. Only the first
    //! call configures; later ones report ConfigurationChangeDeferred.
    //!
    //! \return transport in use, NONE when the parameters were rejected
    ByteStreamTransport configure();

    //! \brief start the read and reconnect tasks, configuring first if needed
    //!
    //! A no-op when the configuration was rejected.
    void start(const FwTaskPriorityType priority = Os::Task::TASK_PRIORITY_DEFAULT,
               const Os::Task::ParamType stack = Os::Task::TASK_DEFAULT,
               const Os::Task::ParamType cpuAffinity = Os::Task::TASK_DEFAULT);

    //! \brief stop the tasks and close the transport; safe if never started
    void stop();

    //! \brief wait for the tasks to finish; OP_OK if never started
    Os::Task::Status join();

    //! \brief transport in use, NONE until configured or when rejected
    ByteStreamTransport getTransport() const;

    //! \brief local port the transport is bound to, 0 when it binds none or is not open
    //!
    //! A TCP client and a serial line bind no port of their own. A UDP link and a TCP
    //! listener report the port they were given, which for a zero port is the ephemeral one
    //! the system assigned once the transport opened.
    U16 getLocalPort();

  protected:
    IpSocket& getSocketHandler() override;

    Fw::Buffer getBuffer() override;

    void sendBuffer(Fw::Buffer buffer, SocketIpStatus status) override;

    void connected() override;

    //! \brief read loop adapted to the configuration
    //!
    //! A TCP listener brings its listening socket up first and tears it down at the end.
    //! Every other configuration reads through the helper's loop unchanged.
    void readLoop() override;

    void parametersLoaded() override;

    void parameterUpdated(FwPrmIdType id) override;

  private:
    Drv::ByteStreamStatus send_handler(const FwIndexType portNum, Fw::Buffer& fwBuffer) override;

    void recvReturnIn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) override;

    void run_handler(FwIndexType portNum, U32 context) override;

    //! \brief parameters read as a set, so validation sees one consistent view
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

        //! \brief whether an endpoint's address is anything other than 0.0.0.0
        static bool hasAddress(const IpEndpoint& endpoint) {
            const IpEndpoint::Type_of_address& octets = endpoint.get_address();
            return (octets[0] != 0) || (octets[1] != 0) || (octets[2] != 0) || (octets[3] != 0);
        }

        //! \brief whether a local endpoint asks for anything beyond the wildcard bind
        //!
        //! The local endpoint is always bound, so this does not decide whether to use it.
        //! It only distinguishes a bind that was asked for from the wildcard default.
        bool localRequested() const {
            return Parameters::hasAddress(this->localEndpoint) || (this->localEndpoint.get_port() != 0);
        }

        //! \brief whether a destination was supplied at all
        bool hasRemote() const {
            return Parameters::hasAddress(this->remoteEndpoint) || (this->remoteEndpoint.get_port() != 0);
        }

        //! \brief whether the destination supplied is one a transport could reach
        bool remoteReachable() const {
            return Parameters::hasAddress(this->remoteEndpoint) && (this->remoteEndpoint.get_port() != 0);
        }

        bool hasSerial() const { return this->serialDevice.length() > 0; }
    };

    Parameters readParameters();

    //! \return transport in use, NONE when the combination was rejected
    ByteStreamTransport configureTcp(const Parameters& parameters);
    ByteStreamTransport configureUdp(const Parameters& parameters);
    ByteStreamTransport configureSerial(const Parameters& parameters);

    //! \brief report a rejected configuration
    //! \return NONE, so callers can return this directly
    ByteStreamTransport reject(const ByteStreamTransport transport, const ByteStreamConfigError error) const;

    //! \brief render an endpoint's octets as the dotted-quad string the sockets take
    static void formatAddress(const IpEndpoint& endpoint, Fw::String& address);

    //! \brief build the endpoint description reported in events
    //!
    //! Prefers the port actually bound over the requested one, so an ephemeral port shows
    //! up as the port the system assigned rather than as 0.
    void buildEndpoint();

    SocketIpStatus startupServer();
    void terminateServer();

    TcpClientSocket m_tcpClient;
    TcpServerSocket m_tcpServer;
    UdpSocket m_udp;
    SerialStream m_serial;

    Parameters m_parameters;  //!< snapshot the configuration was resolved from
    ByteStreamTransport m_transport = ByteStreamTransport::NONE;
    bool m_listening = false;  //!< whether a TCP transport listens rather than connects
    Fw::String m_endpoint;     //!< endpoint description reported in events
    FwSizeType m_allocationSize = 0;
    bool m_configured = false;
    bool m_started = false;

    std::atomic<FwSizeType> m_bytesSent{0};
    std::atomic<FwSizeType> m_bytesReceived{0};
};

}  // namespace Drv

#endif  // DRV_UNIFIEDBYTESTREAMDRIVER_HPP
