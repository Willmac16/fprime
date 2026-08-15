// ======================================================================
// \title  UnifiedByteStreamDriver.hpp
// \brief  hpp file for UnifiedByteStreamDriver component implementation class
// ======================================================================

#ifndef DRV_UNIFIEDBYTESTREAMDRIVER_HPP
#define DRV_UNIFIEDBYTESTREAMDRIVER_HPP

#include <Drv/Ip/IpSocket.hpp>
#include <Drv/Ip/SocketComponentHelper.hpp>
#include <Drv/Ip/TcpClientSocket.hpp>
#include <Drv/Ip/TcpServerSocket.hpp>
#include <Drv/Ip/UdpSocket.hpp>
#include <Drv/UnifiedByteStreamDriver/UnifiedByteStreamDriverComponentAc.hpp>
#include <Fw/Prm/PrmExternalTypes.hpp>
#include <Fw/Types/String.hpp>
#include <Os/Mutex.hpp>
#include <Os/Task.hpp>
#include <atomic>
#include "SerialStream.hpp"

namespace Drv {

/**
 * \brief a byte stream driver whose transport is chosen by parameters
 *
 * Covers what Drv::TcpClient, Drv::TcpServer, Drv::Udp and Drv::LinuxUartDriver cover
 * individually. TRANSPORT names the transport outright:
 *
 * | TRANSPORT   | uses                                                             |
 * |-------------|------------------------------------------------------------------|
 * | TCP_CLIENT  | binds LOCAL_ENDPOINT if asked for, connects to REMOTE_ENDPOINT   |
 * | TCP_SERVER  | listens on LOCAL_ENDPOINT                                        |
 * | UDP         | binds LOCAL_ENDPOINT, sends to REMOTE_ENDPOINT when there is one  |
 * | SERIAL      | opens SERIAL_DEVICE                                              |
 *
 * Zero means what it already means to the transports. A local endpoint is bound, so
 * 0.0.0.0 binds every interface and a zero port takes an ephemeral one. A remote endpoint
 * is a destination, so an entirely zero one is no destination at all.
 *
 * The parameters are external: this component holds them, so `setConfiguration` from a
 * topology and a `param set` from the ground write the same values, and there is no second
 * copy to keep in step. A parameter that changes while the driver is running tears the
 * transport down and brings it back up on the new values.
 */
class UnifiedByteStreamDriver final : public UnifiedByteStreamDriverComponentBase,
                                      public SocketComponentHelper,
                                      public Fw::ParamExternalDelegate {
    friend class UnifiedByteStreamDriverTester;

  public:
    explicit UnifiedByteStreamDriver(const char* const compName);
    ~UnifiedByteStreamDriver() override;

    // ----------------------------------------------------------------------
    // Configuration
    // ----------------------------------------------------------------------

    //! \brief set the transport and its endpoints directly, without a parameter database
    //!
    //! Writes the same storage the parameters use, so a later `param set` from the ground
    //! overwrites this and a `param save` saves what was set here.
    void setConfiguration(const ByteStreamTransport transport,
                          const IpEndpoint& localEndpoint,
                          const IpEndpoint& remoteEndpoint);

    //! \brief set the serial line settings directly
    void setSerialConfiguration(const Fw::StringBase& device,
                                const SerialBaudRate baudRate,
                                const SerialParity parity,
                                const SerialFlowControl flowControl,
                                const U8 readTimeout);

    //! \brief set the buffer size and send timeout directly
    void setBufferConfiguration(const FwSizeType recvBufferSize, const SendTimeout& sendTimeout);

    //! \brief resolve the configuration into a transport, without opening it
    //!
    //!
    //! Called automatically when parameters load and on the first `start`.
    //!
    //! \return transport in use, NONE when the configuration was rejected
    ByteStreamTransport configure();

    // ----------------------------------------------------------------------
    // Lifecycle
    // ----------------------------------------------------------------------

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
    //! Also reported as the LocalPort channel, which is how a ground system learns the
    //! ephemeral port a wildcard bind was given.
    U16 getLocalPort();

    // ----------------------------------------------------------------------
    // Fw::ParamExternalDelegate
    // ----------------------------------------------------------------------

    Fw::SerializeStatus deserializeParam(const FwPrmIdType base_id,
                                         const FwPrmIdType local_id,
                                         const Fw::ParamValid prmStat,
                                         Fw::SerialBufferBase& buff) override;

    Fw::SerializeStatus serializeParam(const FwPrmIdType base_id,
                                       const FwPrmIdType local_id,
                                       Fw::SerialBufferBase& buff) const override;

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

    //! \return NONE when the combination was rejected
    ByteStreamTransport configureTcpClient();
    ByteStreamTransport configureTcpServer();
    ByteStreamTransport configureUdp();
    ByteStreamTransport configureSerial();

    //! \brief report a rejected configuration
    //! \return NONE, so callers can return this directly
    ByteStreamTransport reject(const ByteStreamConfigError error) const;

    //! \brief whether an endpoint's address is anything other than 0.0.0.0
    static bool hasAddress(const IpEndpoint& endpoint);

    //! \brief whether a destination was supplied at all
    bool hasRemote() const;

    //! \brief whether the destination supplied is one a transport could reach
    bool remoteReachable() const;

    //! \brief render an endpoint's octets as the dotted-quad string the sockets take
    static void formatAddress(const IpEndpoint& endpoint, Fw::String& address);

    //! \brief build the endpoint description reported in events
    void buildEndpoint();

    //! \brief write the resolved configuration out as telemetry
    void reportConfiguration();

    SocketIpStatus startupServer();
    void terminateServer();

    TcpClientSocket m_tcpClient;
    TcpServerSocket m_tcpServer;
    UdpSocket m_udp;
    SerialStream m_serial;

    // Parameter storage. These are the parameters: the autocoded external parameter
    // delegate reads and writes them, and setConfiguration writes the same fields.
    ByteStreamTransport m_transportParam = ByteStreamTransport::NONE;
    IpEndpoint m_localEndpoint;
    IpEndpoint m_remoteEndpoint;
    Fw::ParamString m_serialDevice;
    SerialBaudRate m_serialBaudRate = SerialBaudRate::BAUD_115200;
    SerialParity m_serialParity = SerialParity::PARITY_NONE;
    SerialFlowControl m_serialFlowControl = SerialFlowControl::FLOW_NONE;
    U8 m_serialReadTimeout = 10;
    FwSizeType m_recvBufferSize = 1024;
    SendTimeout m_sendTimeout;

    //! Telemetry and events leave this component from two threads - the read task counts
    //! bytes in and reports receive failures, the sender counts bytes out and reports send
    //! failures - so those writes are serialized against each other.
    mutable Os::Mutex m_downlinkLock;

    //! Guards the parameter storage. The framework writes it before it calls
    //! parameterUpdated, so a set lands while the read task is still running.
    mutable Os::Mutex m_paramLock;

    ByteStreamTransport m_transport = ByteStreamTransport::NONE;  //!< transport resolved
    Fw::String m_endpoint;                                        //!< endpoint description in events
    FwSizeType m_allocationSize = 0;
    bool m_configured = false;
    bool m_started = false;

    // Remembered so a parameter change can bring the tasks back the way they went up
    FwTaskPriorityType m_priority = Os::Task::TASK_PRIORITY_DEFAULT;
    Os::Task::ParamType m_stack = Os::Task::TASK_DEFAULT;
    Os::Task::ParamType m_cpuAffinity = Os::Task::TASK_DEFAULT;

    std::atomic<FwSizeType> m_bytesSent{0};
    std::atomic<FwSizeType> m_bytesReceived{0};
};

}  // namespace Drv

#endif  // DRV_UNIFIEDBYTESTREAMDRIVER_HPP
