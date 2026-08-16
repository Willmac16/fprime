// ======================================================================
// \title  UnifiedByteStreamDriverTester.hpp
// \brief  hpp file for UnifiedByteStreamDriver test harness implementation class
// ======================================================================

#ifndef DRV_UNIFIEDBYTESTREAMDRIVER_TESTER_HPP
#define DRV_UNIFIEDBYTESTREAMDRIVER_TESTER_HPP

#include <Drv/UnifiedByteStreamDriver/UnifiedByteStreamDriver.hpp>
#include <Os/Mutex.hpp>
#include <atomic>
#include "UnifiedByteStreamDriverGTestBase.hpp"

#define SEND_DATA_BUFFER_SIZE 1024

namespace Drv {

class UnifiedByteStreamDriverTester : public UnifiedByteStreamDriverGTestBase {
    // Maximum size of histories storing events, telemetry, and port outputs
    static const FwSizeType MAX_HISTORY_SIZE = 1000;
    // Instance ID supplied to the component instance under test
    static const FwEnumStoreType TEST_INSTANCE_ID = 0;

  public:
    // ----------------------------------------------------------------------
    // Construction and destruction
    // ----------------------------------------------------------------------

    UnifiedByteStreamDriverTester();
    ~UnifiedByteStreamDriverTester();

  public:
    // ----------------------------------------------------------------------
    // Configuration resolution tests
    // ----------------------------------------------------------------------

    //! No transport selected leaves the driver disabled with a warning
    void test_transport_none();

    //! TCP_CLIENT connects to the remote endpoint
    void test_tcp_client_configuration();

    //! TCP_CLIENT with no destination has nothing to connect to
    void test_tcp_client_missing_remote_rejected();

    //! TCP_SERVER listens on the local endpoint
    void test_tcp_server_configuration();

    //! TCP_SERVER with nothing supplied listens on every interface, ephemeral port
    void test_tcp_server_wildcard_listen();

    //! A destination and a local endpoint make a bidirectional UDP link
    void test_udp_bidirectional_configuration();

    //! A local endpoint alone makes a UDP link that replies to its last sender
    void test_udp_receive_only_configuration();

    //! UDP with nothing supplied binds every interface on an ephemeral port
    void test_udp_wildcard_bind();

    //! A remote address with no port is neither a destination nor unset
    void test_incomplete_remote_rejected();

    //! A remote port with no address is neither a destination nor unset
    void test_remote_port_without_address_rejected();

    //! A serial device makes a serial link
    void test_serial_configuration();

    //! SERIAL without a device is rejected
    void test_serial_missing_device_rejected();

    //! A zero receive buffer size is rejected before any transport is touched
    void test_invalid_buffer_size_rejected();

    //! A send timeout of a full second or more in the microseconds field is rejected
    void test_invalid_send_timeout_rejected();

    //! An unconfigured driver refuses to send rather than reaching for a transport
    void test_unconfigured_driver_refuses_send();

    //! Every parameter value is reported as telemetry when the configuration resolves
    void test_configuration_telemetry();

    //! A parameter changed after configuration rebuilds the transport on the new value
    void test_parameter_update_reconfigures();

    //! A parameter changed while the driver is running takes effect across a restart
    void test_parameter_update_while_running();

    // ----------------------------------------------------------------------
    // Behavior tests
    // ----------------------------------------------------------------------

    //! The transport can change to a listener while the read task is running
    void test_transport_switch_while_running();

    //! And moving off a listener gives its port back
    void test_transport_switch_releases_listener();

    //! An ephemeral port is reported once the system has assigned it
    void test_ephemeral_port_reported();

    //! Data flows both ways over a parameter-configured UDP link
    void test_udp_messaging();

    //! Data flows both ways over a TCP link that connects out
    void test_tcp_client_messaging();

    //! Data flows both ways over a TCP link that listens
    void test_tcp_server_messaging();

    //! Data flows both ways over a parameter-configured serial link, using a pty as the
    //! device so that the test needs no real hardware
    void test_serial_messaging();

    //! A UDP link given only a destination still binds, so the peer's reply reaches it on
    //! the ephemeral port that bind took
    void test_udp_remote_only_messaging();

    //! Buffers handed back on recvReturnIn are deallocated
    void test_buffer_deallocation();

    //! A buffer the allocator could not serve is not passed on, and not leaked
    void test_failed_allocation_returns_buffer();

    //! A failure that repeats is reported once per throttle window, not once and never again
    void test_repeated_failure_throttled();

    //! The byte counters are pushed as they change
    void test_byte_counter_telemetry();

  private:
    // ----------------------------------------------------------------------
    // Handler overrides for typed from ports
    // ----------------------------------------------------------------------

    void from_recv_handler(const FwIndexType portNum,
                           Fw::Buffer& recvBuffer,
                           const ByteStreamStatus& recvStatus) override;

    Fw::Buffer from_allocate_handler(const FwIndexType portNum, FwSizeType size) override;

    void from_deallocate_handler(const FwIndexType portNum, Fw::Buffer& fwBuffer) override;

  private:
    // ----------------------------------------------------------------------
    // Helper methods
    // ----------------------------------------------------------------------

    //! Connect ports
    void connectPorts();

    //! Initialize components
    void initComponents();

    //! Build a loopback endpoint on the given port
    static IpEndpoint loopback(const U16 port);

    //! An endpoint that is entirely zero
    static IpEndpoint unset();

    //! The serial settings these tests use, on the given device
    static SerialConfig serial(const char* const device);

    //! An arbitrary endpoint
    static IpEndpoint endpoint(const U8 a, const U8 b, const U8 c, const U8 d, const U16 port);

    //! Set every parameter the driver reads, so that no test depends on another's leftovers
    void setParameters(const ByteStreamTransport transport,
                       const IpEndpoint& localEndpoint,
                       const IpEndpoint& remoteEndpoint,
                       const char* const serialDevice = "");

    //! Push the parameters into the component and resolve them
    ByteStreamTransport loadAndConfigure();

    //! Wait for the driver's transport to reach the given open state
    bool wait_on_open(bool open, U32 iterations);

    //! Wait for the receive handler to see a buffer
    bool wait_on_receive(U32 iterations);

  private:
    // ----------------------------------------------------------------------
    // Variables
    // ----------------------------------------------------------------------

    //! The component under test
    UnifiedByteStreamDriver component;
    //! Buffer of data sent through the driver, compared against what comes back
    Fw::Buffer m_data_buffer;
    //! Protects m_data_buffer, which is shared with the receive thread's handler
    Os::Mutex m_buffer_lock;
    U8 m_data_storage[SEND_DATA_BUFFER_SIZE];
    //! Set by the receive handler once a matching buffer has arrived
    std::atomic<bool> m_received;
    //! When set, the allocator hands back a buffer the driver cannot use
    bool m_starve_allocator;
    //! Buffers handed out by the allocator and not yet returned
    std::atomic<I32> m_outstanding_buffers;
};

}  // end namespace Drv

#endif
