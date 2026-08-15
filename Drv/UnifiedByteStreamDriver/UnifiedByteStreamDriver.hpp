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
 * | TCP_CLIENT  | connects to REMOTE_ENDPOINT                                      |
 * | TCP_SERVER  | listens on LOCAL_ENDPOINT                                        |
 * | UDP         | binds LOCAL_ENDPOINT, sends to REMOTE_ENDPOINT when there is one  |
 * | SERIAL      | opens SERIAL_CONFIG.device                                       |
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
    void setSerialConfiguration(const SerialConfig& serialConfig);

    //! \brief set the buffer size and send timeout directly
    void setBufferConfiguration(const FwSizeType recvBufferSize, const SendTimeout& sendTimeout);

    //! \brief resolve the configuration into a transport, without opening it
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

    void parameterUpdated(FwPrmIdType id) override;

  private:
    Drv::ByteStreamStatus send_handler(const FwIndexType portNum, Fw::Buffer& fwBuffer) override;

    void recvReturnIn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) override;

    //! \brief resolve the configuration; call with the configuration lock held
    ByteStreamTransport applyConfiguration();

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

    //! The parameters and everything resolved from them, with the lock that guards them.
    //!
    //! The read task reaches this through getSocketHandler, which is where a staged change
    //! is applied, so every mutation of the sockets happens on one thread at a time.
    struct Configuration {
        Os::Mutex lock;

        // The parameters themselves: the autocoded external parameter delegate reads and
        // writes these, and the setters write the same fields.
        ByteStreamTransport transportParam = ByteStreamTransport::NONE;
        IpEndpoint localEndpoint;
        IpEndpoint remoteEndpoint;
        SerialConfig serial;
        FwSizeType recvBufferSize = 1024;
        SendTimeout sendTimeout;

        ByteStreamTransport transport = ByteStreamTransport::NONE;  //!< resolved transport
        Fw::String endpoint;                                        //!< endpoint description in events
        FwSizeType allocationSize = 0;
        bool resolved = false;  //!< whether the parameters have been resolved at least once
        bool started = false;   //!< whether the read task is running, so a change must be staged
        bool reconfigurePending = false;
        //! Set while parameterUpdated is tearing the link down. The teardown calls reach
        //! getSocketHandler from the caller's thread, and applying there would be the very
        //! cross-thread write the hand-off exists to avoid.
        bool suppressApply = false;
    };
    mutable Configuration m_config;

    //! Everything that leaves this component downwards, with the lock that guards it.
    //!
    //! Telemetry is written from two threads - the read task counts bytes in, the sender
    //! counts bytes out - so those writes and the counters behind them are serialized.
    struct Downlink {
        Os::Mutex lock;
        FwSizeType bytesSent = 0;
        FwSizeType bytesReceived = 0;
    };
    mutable Downlink m_downlink;

    //! \brief whether the read task is running
    bool isStarted() const;
};

}  // namespace Drv

#endif  // DRV_UNIFIEDBYTESTREAMDRIVER_HPP
