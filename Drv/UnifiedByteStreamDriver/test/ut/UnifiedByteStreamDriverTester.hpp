// ======================================================================
// \title  UnifiedByteStreamDriverTester.hpp
// \author fprime
// \brief  hpp file for UnifiedByteStreamDriver test harness implementation class
//
// \copyright
// Copyright 2009-2025, by the California Institute of Technology.
// ALL RIGHTS RESERVED.  United States Government Sponsorship
// acknowledged.
//
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

    //! A remote endpoint alone resolves to a TCP client
    void test_tcp_client_configuration();

    //! A local endpoint alone resolves to a TCP server
    void test_tcp_server_configuration();

    //! Both endpoints at once is not a TCP configuration this driver can serve
    void test_tcp_both_endpoints_rejected();

    //! TCP with no endpoint at all is rejected
    void test_tcp_no_endpoint_rejected();

    //! A TCP client cannot connect to port 0
    void test_tcp_missing_remote_port_rejected();

    //! Host names are rejected: the IP transports do not resolve them
    void test_non_dotted_quad_rejected();

    //! Both endpoints resolve to a bidirectional UDP link
    void test_udp_bidirectional_configuration();

    //! A local endpoint alone resolves to a UDP link that replies to its last sender
    void test_udp_receive_only_configuration();

    //! A remote endpoint alone resolves to a send-only UDP link
    void test_udp_send_only_configuration();

    //! UDP with no endpoint at all is rejected
    void test_udp_no_endpoint_rejected();

    //! A serial device resolves to a serial link
    void test_serial_configuration();

    //! SERIAL without a device is rejected
    void test_serial_missing_device_rejected();

    //! IP parameters do not apply to a serial link and are reported as ignored
    void test_serial_ignores_ip_parameters();

    //! Serial parameters do not apply to an IP link and are reported as ignored
    void test_ip_ignores_serial_parameters();

    //! A zero receive buffer size is rejected before any transport is touched
    void test_invalid_buffer_size_rejected();

    //! A send timeout of a full second or more in the microseconds field is rejected
    void test_invalid_send_timeout_rejected();

    //! Configuration is resolved once: later changes are reported as deferred
    void test_configuration_change_deferred();

    //! A disabled driver refuses to send rather than reaching for a transport
    void test_disabled_driver_refuses_send();

    //! Address validation accepts dotted quads and nothing else
    void test_dotted_quad_validation();

    // ----------------------------------------------------------------------
    // Behavior tests
    // ----------------------------------------------------------------------

    //! Data flows both ways over a parameter-configured UDP link
    void test_udp_messaging();

    //! Data flows both ways over a parameter-configured TCP client link
    void test_tcp_client_messaging();

    //! Data flows both ways over a parameter-configured TCP server link
    void test_tcp_server_messaging();

    //! Data flows both ways over a parameter-configured serial link, using a pty as the
    //! device so that the test needs no real hardware
    void test_serial_messaging();

    //! Buffers handed back on recvReturnIn are deallocated
    void test_buffer_deallocation();

    //! Telemetry reports the resolved mode and the byte counters
    void test_telemetry();

  private:
    // ----------------------------------------------------------------------
    // Handler overrides for typed from ports
    // ----------------------------------------------------------------------

    void from_recv_handler(const FwIndexType portNum,
                           Fw::Buffer& recvBuffer,
                           const ByteStreamStatus& recvStatus) override;

    Fw::Buffer from_allocate_handler(const FwIndexType portNum, FwSizeType size) override;

  private:
    // ----------------------------------------------------------------------
    // Helper methods
    // ----------------------------------------------------------------------

    //! Connect ports
    void connectPorts();

    //! Initialize components
    void initComponents();

    //! Set every parameter the driver reads, so that no test depends on another's leftovers
    void setParameters(const ByteStreamTransport transport,
                       const char* const localAddress,
                       const U16 localPort,
                       const char* const remoteAddress,
                       const U16 remotePort,
                       const char* const serialDevice = "");

    //! Push the parameters into the component and resolve them
    ByteStreamDriverMode loadAndConfigure();

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
};

}  // end namespace Drv

#endif  // DRV_UNIFIEDBYTESTREAMDRIVER_TESTER_HPP
