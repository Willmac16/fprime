// ======================================================================
// \title  UnifiedByteStreamDriverTester.cpp
// \author fprime
// \brief  cpp file for UnifiedByteStreamDriver test harness implementation class
//
// \copyright
// Copyright 2009-2025, by the California Institute of Technology.
// ALL RIGHTS RESERVED.  United States Government Sponsorship
// acknowledged.
//
// ======================================================================

#include "UnifiedByteStreamDriverTester.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <Drv/Ip/TcpClientSocket.hpp>
#include <Drv/Ip/TcpServerSocket.hpp>
#include <Drv/Ip/UdpSocket.hpp>
#include <Drv/Ip/test/ut/PortSelector.hpp>
#include <Drv/Ip/test/ut/SocketTestHelper.hpp>
#include <cstdlib>
#include <cstring>
#include "Os/Console.hpp"

Os::Console logger;

namespace Drv {

namespace {
//! Dotted-quad form of the loopback endpoints these tests use
const char* const LOOPBACK = "127.0.0.1";
//! Serial device path used by the configuration tests. Nothing is opened, so the device
//! does not need to exist on the machine running the tests.
const char* const TEST_DEVICE = "/dev/ttyUSB0";
//! Iterations used when polling for an asynchronous state change
const U32 WAIT_ITERATIONS = 1000;
}  // namespace

// ----------------------------------------------------------------------
// Construction and destruction
// ----------------------------------------------------------------------

UnifiedByteStreamDriverTester::UnifiedByteStreamDriverTester()
    : UnifiedByteStreamDriverGTestBase("Tester", MAX_HISTORY_SIZE),
      component("UnifiedByteStreamDriver"),
      m_data_buffer(m_data_storage, sizeof(m_data_storage)),
      m_received(false) {
    this->initComponents();
    this->connectPorts();
    (void)::memset(this->m_data_storage, 0, sizeof(this->m_data_storage));
}

UnifiedByteStreamDriverTester::~UnifiedByteStreamDriverTester() {}

// ----------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------

IpEndpoint UnifiedByteStreamDriverTester::loopback(const U16 port) {
    const U8 address[4] = {127, 0, 0, 1};
    return IpEndpoint(address, port);
}

IpEndpoint UnifiedByteStreamDriverTester::unset() {
    const U8 address[4] = {0, 0, 0, 0};
    return IpEndpoint(address, 0);
}

IpEndpoint UnifiedByteStreamDriverTester::endpoint(const U8 a, const U8 b, const U8 c, const U8 d, const U16 port) {
    const U8 address[4] = {a, b, c, d};
    return IpEndpoint(address, port);
}

void UnifiedByteStreamDriverTester::setParameters(const ByteStreamTransport transport,
                                                  const IpEndpoint& localEndpoint,
                                                  const IpEndpoint& remoteEndpoint,
                                                  const char* const serialDevice) {
    this->paramSet_TRANSPORT(transport, Fw::ParamValid::VALID);
    this->paramSet_LOCAL_ENDPOINT(localEndpoint, Fw::ParamValid::VALID);
    this->paramSet_REMOTE_ENDPOINT(remoteEndpoint, Fw::ParamValid::VALID);
    Fw::String device(serialDevice);
    this->paramSet_SERIAL_DEVICE(device, Fw::ParamValid::VALID);
    this->paramSet_SERIAL_BAUD_RATE(SerialBaudRate::BAUD_115200, Fw::ParamValid::VALID);
    this->paramSet_SERIAL_PARITY(SerialParity::PARITY_NONE, Fw::ParamValid::VALID);
    this->paramSet_SERIAL_FLOW_CONTROL(SerialFlowControl::FLOW_NONE, Fw::ParamValid::VALID);
    const FwSizeType bufferSize = sizeof(this->m_data_storage);
    this->paramSet_RECV_BUFFER_SIZE(bufferSize, Fw::ParamValid::VALID);
    const U32 timeoutSeconds = 0;
    const U32 timeoutMicroseconds = 100;
    this->paramSet_SEND_TIMEOUT_SECONDS(timeoutSeconds, Fw::ParamValid::VALID);
    this->paramSet_SEND_TIMEOUT_MICROSECONDS(timeoutMicroseconds, Fw::ParamValid::VALID);
}

ByteStreamTransport UnifiedByteStreamDriverTester::loadAndConfigure() {
    // loadParameters pulls every parameter over the prmGet port and then calls
    // parametersLoaded, which is where the driver resolves its configuration
    this->component.loadParameters();
    return this->component.getTransport();
}

bool UnifiedByteStreamDriverTester::wait_on_open(bool open, U32 iterations) {
    for (U32 i = 0; i < iterations; i++) {
        if (open == this->component.isOpened()) {
            return true;
        }
        (void)Os::Task::delay(Fw::TimeInterval(0, 10000));
    }
    return false;
}

bool UnifiedByteStreamDriverTester::wait_on_receive(U32 iterations) {
    for (U32 i = 0; i < iterations; i++) {
        if (this->m_received) {
            return true;
        }
        (void)Os::Task::delay(Fw::TimeInterval(0, 10000));
    }
    return false;
}

// ----------------------------------------------------------------------
// Configuration resolution tests
// ----------------------------------------------------------------------

void UnifiedByteStreamDriverTester::test_transport_none() {
    this->setParameters(ByteStreamTransport::NONE, unset(), unset());
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::NONE);
    ASSERT_EVENTS_TransportNotConfigured_SIZE(1);
    ASSERT_EVENTS_ConfigurationApplied_SIZE(0);
}

void UnifiedByteStreamDriverTester::test_tcp_connect_configuration() {
    this->setParameters(ByteStreamTransport::TCP, unset(), loopback(50000));
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::TCP);
    ASSERT_EVENTS_ConfigurationApplied_SIZE(1);
    ASSERT_EVENTS_ConfigurationApplied(0, ByteStreamTransport::TCP, "connect 127.0.0.1:50000");
    ASSERT_EVENTS_UnsupportedConfiguration_SIZE(0);
    // A connecting socket has no local port of its own to report
    ASSERT_EQ(this->component.getLocalPort(), 0);
}

void UnifiedByteStreamDriverTester::test_tcp_listen_configuration() {
    this->setParameters(ByteStreamTransport::TCP, loopback(50001), unset());
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::TCP);
    ASSERT_EVENTS_ConfigurationApplied_SIZE(1);
    ASSERT_EVENTS_ConfigurationApplied(0, ByteStreamTransport::TCP, "listen 127.0.0.1:50001");
    ASSERT_EVENTS_UnsupportedConfiguration_SIZE(0);
}

void UnifiedByteStreamDriverTester::test_tcp_both_endpoints_rejected() {
    this->setParameters(ByteStreamTransport::TCP, loopback(50002), loopback(50003));
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::NONE);
    ASSERT_EVENTS_UnsupportedConfiguration_SIZE(1);
    ASSERT_EVENTS_UnsupportedConfiguration(0, ByteStreamTransport::TCP, ByteStreamConfigError::TCP_LOCAL_AND_REMOTE);
    ASSERT_EVENTS_ConfigurationApplied_SIZE(0);
}

void UnifiedByteStreamDriverTester::test_tcp_no_endpoint_rejected() {
    this->setParameters(ByteStreamTransport::TCP, unset(), unset());
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::NONE);
    ASSERT_EVENTS_UnsupportedConfiguration_SIZE(1);
    ASSERT_EVENTS_UnsupportedConfiguration(0, ByteStreamTransport::TCP, ByteStreamConfigError::NO_ENDPOINT);
}

void UnifiedByteStreamDriverTester::test_udp_bidirectional_configuration() {
    this->setParameters(ByteStreamTransport::UDP, loopback(50005), loopback(50006));
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::UDP);
    ASSERT_EVENTS_ConfigurationApplied_SIZE(1);
    ASSERT_EVENTS_ConfigurationApplied(0, ByteStreamTransport::UDP, "bind 127.0.0.1:50005 send 127.0.0.1:50006");
}

void UnifiedByteStreamDriverTester::test_udp_receive_only_configuration() {
    this->setParameters(ByteStreamTransport::UDP, loopback(50007), unset());
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::UDP);
    ASSERT_EVENTS_ConfigurationApplied_SIZE(1);
    ASSERT_EVENTS_ConfigurationApplied(0, ByteStreamTransport::UDP, "bind 127.0.0.1:50007");
    ASSERT_TRUE(this->component.m_receiveEnabled);
}

void UnifiedByteStreamDriverTester::test_udp_send_only_configuration() {
    this->setParameters(ByteStreamTransport::UDP, unset(), loopback(50008));
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::UDP);
    ASSERT_EVENTS_ConfigurationApplied_SIZE(1);
    ASSERT_EVENTS_ConfigurationApplied(0, ByteStreamTransport::UDP, "send 127.0.0.1:50008");
    // Nothing is bound locally, so there is no receive direction to read
    ASSERT_FALSE(this->component.m_receiveEnabled);
}

void UnifiedByteStreamDriverTester::test_udp_no_endpoint_rejected() {
    this->setParameters(ByteStreamTransport::UDP, unset(), unset());
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::NONE);
    ASSERT_EVENTS_UnsupportedConfiguration_SIZE(1);
    ASSERT_EVENTS_UnsupportedConfiguration(0, ByteStreamTransport::UDP, ByteStreamConfigError::NO_ENDPOINT);
}

void UnifiedByteStreamDriverTester::test_serial_configuration() {
    this->setParameters(ByteStreamTransport::SERIAL, unset(), unset(), TEST_DEVICE);
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::SERIAL);
    ASSERT_EVENTS_ConfigurationApplied_SIZE(1);
    ASSERT_EVENTS_ConfigurationApplied(0, ByteStreamTransport::SERIAL, "/dev/ttyUSB0 @ 115200");
    ASSERT_EVENTS_IgnoredConfiguration_SIZE(0);
}

void UnifiedByteStreamDriverTester::test_serial_missing_device_rejected() {
    this->setParameters(ByteStreamTransport::SERIAL, unset(), unset(), "");
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::NONE);
    ASSERT_EVENTS_UnsupportedConfiguration_SIZE(1);
    ASSERT_EVENTS_UnsupportedConfiguration(0, ByteStreamTransport::SERIAL, ByteStreamConfigError::NO_SERIAL_DEVICE);
}

void UnifiedByteStreamDriverTester::test_serial_ignores_ip_parameters() {
    this->setParameters(ByteStreamTransport::SERIAL, loopback(50009), unset(), TEST_DEVICE);
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::SERIAL);
    ASSERT_EVENTS_IgnoredConfiguration_SIZE(1);
    ASSERT_EVENTS_IgnoredConfiguration(0, ByteStreamConfigGroup::IP, ByteStreamTransport::SERIAL);
    // Ignoring parameters is a warning, not a rejection: the serial link still comes up
    ASSERT_EVENTS_ConfigurationApplied_SIZE(1);
}

void UnifiedByteStreamDriverTester::test_ip_ignores_serial_parameters() {
    this->setParameters(ByteStreamTransport::UDP, loopback(50010), unset(), TEST_DEVICE);
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::UDP);
    ASSERT_EVENTS_IgnoredConfiguration_SIZE(1);
    ASSERT_EVENTS_IgnoredConfiguration(0, ByteStreamConfigGroup::SERIAL, ByteStreamTransport::UDP);
    ASSERT_EVENTS_ConfigurationApplied_SIZE(1);
}

void UnifiedByteStreamDriverTester::test_invalid_buffer_size_rejected() {
    this->setParameters(ByteStreamTransport::UDP, loopback(50011), unset());
    const FwSizeType zero = 0;
    this->paramSet_RECV_BUFFER_SIZE(zero, Fw::ParamValid::VALID);
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::NONE);
    ASSERT_EVENTS_UnsupportedConfiguration_SIZE(1);
    ASSERT_EVENTS_UnsupportedConfiguration(0, ByteStreamTransport::UDP, ByteStreamConfigError::INVALID_BUFFER_SIZE);
}

void UnifiedByteStreamDriverTester::test_invalid_send_timeout_rejected() {
    this->setParameters(ByteStreamTransport::UDP, loopback(50012), unset());
    const U32 tooLarge = 1000000;
    this->paramSet_SEND_TIMEOUT_MICROSECONDS(tooLarge, Fw::ParamValid::VALID);
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::NONE);
    ASSERT_EVENTS_UnsupportedConfiguration_SIZE(1);
    ASSERT_EVENTS_UnsupportedConfiguration(0, ByteStreamTransport::UDP, ByteStreamConfigError::INVALID_SEND_TIMEOUT);
}

void UnifiedByteStreamDriverTester::test_configuration_change_deferred() {
    this->setParameters(ByteStreamTransport::UDP, loopback(50013), unset());
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::UDP);
    ASSERT_EVENTS_ConfigurationChangeDeferred_SIZE(0);

    // A second resolution must not disturb the configuration already in place
    this->setParameters(ByteStreamTransport::SERIAL, unset(), unset(), TEST_DEVICE);
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::UDP);
    ASSERT_EVENTS_ConfigurationChangeDeferred_SIZE(1);
    ASSERT_EVENTS_ConfigurationApplied_SIZE(1);
}

void UnifiedByteStreamDriverTester::test_unconfigured_driver_refuses_send() {
    this->setParameters(ByteStreamTransport::NONE, unset(), unset());
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::NONE);

    U8 data[8] = {};
    Fw::Buffer buffer(data, sizeof(data));
    ASSERT_EQ(this->invoke_to_send(0, buffer), ByteStreamStatus::OTHER_ERROR);

    // Starting an unconfigured driver is a no-op rather than an error
    this->component.start();
    ASSERT_EQ(this->component.join(), Os::Task::Status::OP_OK);
}

void UnifiedByteStreamDriverTester::test_wildcard_address_is_set() {
    // 0.0.0.0 binds every interface: a wildcard address, not an unset endpoint
    this->setParameters(ByteStreamTransport::TCP, endpoint(0, 0, 0, 0, 50015), unset());
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::TCP);
    ASSERT_EVENTS_ConfigurationApplied(0, ByteStreamTransport::TCP, "listen 0.0.0.0:50015");
}

void UnifiedByteStreamDriverTester::test_wildcard_local_port_is_set() {
    // A zero port asks for an ephemeral one, so the endpoint is still set
    this->setParameters(ByteStreamTransport::UDP, loopback(0), unset());
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::UDP);
    ASSERT_EVENTS_ConfigurationApplied(0, ByteStreamTransport::UDP, "bind 127.0.0.1:0");
    ASSERT_TRUE(this->component.m_receiveEnabled);
}

void UnifiedByteStreamDriverTester::test_tcp_wildcard_remote_port_rejected() {
    this->setParameters(ByteStreamTransport::TCP, unset(), loopback(0));
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::NONE);
    ASSERT_EVENTS_UnsupportedConfiguration(0, ByteStreamTransport::TCP, ByteStreamConfigError::MISSING_REMOTE_PORT);
}

void UnifiedByteStreamDriverTester::test_udp_reply_to_sender_needs_local() {
    // A remote wildcard port means "reply to the last sender", which needs a bound local
    this->setParameters(ByteStreamTransport::UDP, unset(), loopback(0));
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::NONE);
    ASSERT_EVENTS_UnsupportedConfiguration(0, ByteStreamTransport::UDP, ByteStreamConfigError::NO_ENDPOINT);

    // With a local endpoint it is a usable reply-to-sender configuration
    Drv::UnifiedByteStreamDriverTester other;
    other.setParameters(ByteStreamTransport::UDP, other.loopback(50016), other.loopback(0));
    ASSERT_EQ(other.loadAndConfigure(), ByteStreamTransport::UDP);
}

void UnifiedByteStreamDriverTester::test_ephemeral_port_reported() {
    this->setParameters(ByteStreamTransport::UDP, loopback(0), unset());
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::UDP);
    ASSERT_EQ(this->component.getLocalPort(), 0);  // nothing assigned until the socket opens

    this->component.start();
    ASSERT_TRUE(this->wait_on_open(true, WAIT_ITERATIONS)) << "Driver never bound its UDP socket";
    const U16 assigned = this->component.getLocalPort();
    ASSERT_NE(assigned, 0) << "Ephemeral port was never assigned";

    // PortOpened carries the assigned port, not the zero that was asked for
    Fw::String expected;
    (void)expected.format("bind 127.0.0.1:%hu", assigned);
    ASSERT_EVENTS_PortOpened(0, ByteStreamTransport::UDP, expected.toChar());

    this->component.stop();
    ASSERT_EQ(this->component.join(), Os::Task::Status::OP_OK);
}

// ----------------------------------------------------------------------
// Behavior tests
// ----------------------------------------------------------------------

void UnifiedByteStreamDriverTester::test_udp_messaging() {
    const U16 driverPort = Drv::Test::get_free_port(true);
    ASSERT_NE(driverPort, 0);
    U16 peerPort = driverPort;
    for (U32 i = 0; (i < 100) && (peerPort == driverPort); i++) {
        peerPort = Drv::Test::get_free_port(true);
    }
    if (peerPort == driverPort) {
        GTEST_SKIP() << "Could not find two free UDP ports";
    }

    this->setParameters(ByteStreamTransport::UDP, loopback(driverPort), loopback(peerPort));
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::UDP);

    // The peer is the mirror image of the driver's configuration
    Drv::UdpSocket peer;
    Drv::SocketDescriptor peerDescriptor;
    ASSERT_EQ(peer.configureRecv(LOOPBACK, peerPort), Drv::SOCK_SUCCESS);
    ASSERT_EQ(peer.configureSend(LOOPBACK, driverPort, 0, 100), Drv::SOCK_SUCCESS);
    ASSERT_EQ(peer.open(peerDescriptor), Drv::SOCK_SUCCESS);

    this->component.start();
    ASSERT_TRUE(this->wait_on_open(true, WAIT_ITERATIONS)) << "Driver never opened its UDP socket";
    // Keep the read thread from blocking forever should a datagram go missing
    Drv::Test::force_recv_timeout(this->component.m_descriptor.fd, this->component.getSocketHandler());
    Drv::Test::force_recv_timeout(peerDescriptor.fd, peer);
    ASSERT_EQ(this->component.getLocalPort(), driverPort);

    // Driver to peer
    U8 received[sizeof(this->m_data_storage)] = {};
    FwSizeType size = 0;
    {
        Os::ScopeLock lock(this->m_buffer_lock);
        this->m_data_buffer.setSize(sizeof(this->m_data_storage));
        size = Drv::Test::fill_random_buffer(this->m_data_buffer);
    }
    ASSERT_EQ(this->invoke_to_send(0, this->m_data_buffer), ByteStreamStatus::OP_OK);
    Drv::Test::receive_all(peer, peerDescriptor, received, size);
    Drv::Test::validate_random_buffer(this->m_data_buffer, received);

    // Peer to driver, checked by the receive handler
    this->m_received = false;
    {
        Os::ScopeLock lock(this->m_buffer_lock);
        this->m_data_buffer.setSize(sizeof(this->m_data_storage));
        (void)Drv::Test::fill_random_buffer(this->m_data_buffer);
        ASSERT_EQ(peer.send(peerDescriptor, this->m_data_buffer.getData(), this->m_data_buffer.getSize()),
                  Drv::SOCK_SUCCESS);
    }
    ASSERT_TRUE(this->wait_on_receive(WAIT_ITERATIONS)) << "Driver never received the peer's datagram";

    this->component.stop();
    ASSERT_EQ(this->component.join(), Os::Task::Status::OP_OK);
    peer.close(peerDescriptor);
    ASSERT_from_ready_SIZE(1);
}

void UnifiedByteStreamDriverTester::test_tcp_connect_messaging() {
    const U16 port = Drv::Test::get_free_port();
    ASSERT_NE(port, 0);

    // Bring the listener up before the driver starts connecting to it
    Drv::TcpServerSocket peer;
    Drv::SocketDescriptor peerDescriptor;
    ASSERT_EQ(peer.configure(LOOPBACK, port, 0, 100), Drv::SOCK_SUCCESS);
    ASSERT_EQ(peer.startup(peerDescriptor), Drv::SOCK_SUCCESS);

    this->setParameters(ByteStreamTransport::TCP, unset(), loopback(port));
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::TCP);

    this->component.start();
    // The accept completes once the driver's reconnect thread has connected
    ASSERT_EQ(peer.open(peerDescriptor), Drv::SOCK_SUCCESS);
    ASSERT_TRUE(this->wait_on_open(true, WAIT_ITERATIONS)) << "Driver never connected";
    Drv::Test::force_recv_timeout(this->component.m_descriptor.fd, this->component.getSocketHandler());
    Drv::Test::force_recv_timeout(peerDescriptor.fd, peer);

    // Driver to peer
    U8 received[sizeof(this->m_data_storage)] = {};
    FwSizeType size = 0;
    {
        Os::ScopeLock lock(this->m_buffer_lock);
        this->m_data_buffer.setSize(sizeof(this->m_data_storage));
        size = Drv::Test::fill_random_buffer(this->m_data_buffer);
    }
    ASSERT_EQ(this->invoke_to_send(0, this->m_data_buffer), ByteStreamStatus::OP_OK);
    Drv::Test::receive_all(peer, peerDescriptor, received, size);
    Drv::Test::validate_random_buffer(this->m_data_buffer, received);

    // Peer to driver
    this->m_received = false;
    {
        Os::ScopeLock lock(this->m_buffer_lock);
        this->m_data_buffer.setSize(sizeof(this->m_data_storage));
        (void)Drv::Test::fill_random_buffer(this->m_data_buffer);
        ASSERT_EQ(peer.send(peerDescriptor, this->m_data_buffer.getData(), this->m_data_buffer.getSize()),
                  Drv::SOCK_SUCCESS);
    }
    ASSERT_TRUE(this->wait_on_receive(WAIT_ITERATIONS)) << "Driver never received the peer's data";

    this->component.stop();
    ASSERT_EQ(this->component.join(), Os::Task::Status::OP_OK);
    peer.terminate(peerDescriptor);
    ASSERT_from_ready_SIZE(1);
}

void UnifiedByteStreamDriverTester::test_tcp_listen_messaging() {
    const U16 port = Drv::Test::get_free_port();
    ASSERT_NE(port, 0);

    this->setParameters(ByteStreamTransport::TCP, loopback(port), unset());
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::TCP);
    this->component.start();

    // Retry the connection: the driver's listener comes up on its read thread
    Drv::TcpClientSocket peer;
    Drv::SocketDescriptor peerDescriptor;
    ASSERT_EQ(peer.configure(LOOPBACK, port, 0, 100), Drv::SOCK_SUCCESS);
    Drv::SocketIpStatus status = Drv::SOCK_FAILED_TO_CONNECT;
    for (U32 i = 0; (i < WAIT_ITERATIONS) && (status != Drv::SOCK_SUCCESS); i++) {
        status = peer.open(peerDescriptor);
        if (status != Drv::SOCK_SUCCESS) {
            (void)Os::Task::delay(Fw::TimeInterval(0, 10000));
        }
    }
    ASSERT_EQ(status, Drv::SOCK_SUCCESS) << "Peer never connected to the driver's listener";
    ASSERT_TRUE(this->wait_on_open(true, WAIT_ITERATIONS)) << "Driver never accepted the connection";
    Drv::Test::force_recv_timeout(this->component.m_descriptor.fd, this->component.getSocketHandler());
    Drv::Test::force_recv_timeout(peerDescriptor.fd, peer);
    ASSERT_EQ(this->component.getLocalPort(), port);

    // Driver to peer
    U8 received[sizeof(this->m_data_storage)] = {};
    FwSizeType size = 0;
    {
        Os::ScopeLock lock(this->m_buffer_lock);
        this->m_data_buffer.setSize(sizeof(this->m_data_storage));
        size = Drv::Test::fill_random_buffer(this->m_data_buffer);
    }
    ASSERT_EQ(this->invoke_to_send(0, this->m_data_buffer), ByteStreamStatus::OP_OK);
    Drv::Test::receive_all(peer, peerDescriptor, received, size);
    Drv::Test::validate_random_buffer(this->m_data_buffer, received);

    // Peer to driver
    this->m_received = false;
    {
        Os::ScopeLock lock(this->m_buffer_lock);
        this->m_data_buffer.setSize(sizeof(this->m_data_storage));
        (void)Drv::Test::fill_random_buffer(this->m_data_buffer);
        ASSERT_EQ(peer.send(peerDescriptor, this->m_data_buffer.getData(), this->m_data_buffer.getSize()),
                  Drv::SOCK_SUCCESS);
    }
    ASSERT_TRUE(this->wait_on_receive(WAIT_ITERATIONS)) << "Driver never received the peer's data";

    this->component.stop();
    ASSERT_EQ(this->component.join(), Os::Task::Status::OP_OK);
    peer.close(peerDescriptor);
    ASSERT_from_ready_SIZE(1);
}

void UnifiedByteStreamDriverTester::test_serial_messaging() {
    // A pseudo-terminal is a serial device as far as termios is concerned, so the driver
    // can be exercised end to end without any real hardware attached
    const int master = ::posix_openpt(O_RDWR | O_NOCTTY);
    ASSERT_NE(master, -1) << "Could not open a pseudo-terminal";
    ASSERT_EQ(::grantpt(master), 0);
    ASSERT_EQ(::unlockpt(master), 0);
    const char* const device = ::ptsname(master);
    ASSERT_NE(device, nullptr);

    this->setParameters(ByteStreamTransport::SERIAL, unset(), unset(), device);
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::SERIAL);

    this->component.start();
    ASSERT_TRUE(this->wait_on_open(true, WAIT_ITERATIONS)) << "Driver never opened " << device;

    // Driver to the far end of the line
    FwSizeType size = 0;
    {
        Os::ScopeLock lock(this->m_buffer_lock);
        this->m_data_buffer.setSize(sizeof(this->m_data_storage));
        size = Drv::Test::fill_random_buffer(this->m_data_buffer);
    }
    ASSERT_EQ(this->invoke_to_send(0, this->m_data_buffer), ByteStreamStatus::OP_OK);
    U8 received[sizeof(this->m_data_storage)] = {};
    FwSizeType total = 0;
    while (total < size) {
        const ssize_t count = ::read(master, &received[total], static_cast<size_t>(size - total));
        ASSERT_GT(count, 0) << "Read from the pseudo-terminal failed";
        total += static_cast<FwSizeType>(count);
    }
    Drv::Test::validate_random_buffer(this->m_data_buffer, received);

    // Far end of the line to the driver. A serial line has no framing, so the data is kept
    // short enough to arrive in a single read.
    this->m_received = false;
    {
        Os::ScopeLock lock(this->m_buffer_lock);
        this->m_data_buffer.setSize(16);
        Drv::Test::fill_random_data(this->m_data_buffer.getData(), this->m_data_buffer.getSize());
        const ssize_t written =
            ::write(master, this->m_data_buffer.getData(), static_cast<size_t>(this->m_data_buffer.getSize()));
        ASSERT_EQ(written, static_cast<ssize_t>(this->m_data_buffer.getSize()));
    }
    ASSERT_TRUE(this->wait_on_receive(WAIT_ITERATIONS)) << "Driver never received data from the line";

    this->component.stop();
    ASSERT_EQ(this->component.join(), Os::Task::Status::OP_OK);
    (void)::close(master);
    ASSERT_from_ready_SIZE(1);
}

void UnifiedByteStreamDriverTester::test_udp_send_only_messaging() {
    const U16 peerPort = Drv::Test::get_free_port(true);
    ASSERT_NE(peerPort, 0);

    // No local endpoint, so the driver has nothing to read and holds the transport open for
    // the send path alone
    this->setParameters(ByteStreamTransport::UDP, unset(), loopback(peerPort));
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::UDP);
    ASSERT_FALSE(this->component.m_receiveEnabled);

    Drv::UdpSocket peer;
    Drv::SocketDescriptor peerDescriptor;
    ASSERT_EQ(peer.configureRecv(LOOPBACK, peerPort), Drv::SOCK_SUCCESS);
    ASSERT_EQ(peer.open(peerDescriptor), Drv::SOCK_SUCCESS);

    this->component.start();
    ASSERT_TRUE(this->wait_on_open(true, WAIT_ITERATIONS)) << "Driver never opened its send-only UDP socket";
    Drv::Test::force_recv_timeout(peerDescriptor.fd, peer);
    // Nothing is bound locally, so there is no local port to report
    ASSERT_EQ(this->component.getLocalPort(), 0);

    U8 received[sizeof(this->m_data_storage)] = {};
    FwSizeType size = 0;
    {
        Os::ScopeLock lock(this->m_buffer_lock);
        this->m_data_buffer.setSize(sizeof(this->m_data_storage));
        size = Drv::Test::fill_random_buffer(this->m_data_buffer);
    }
    ASSERT_EQ(this->invoke_to_send(0, this->m_data_buffer), ByteStreamStatus::OP_OK);
    Drv::Test::receive_all(peer, peerDescriptor, received, size);
    Drv::Test::validate_random_buffer(this->m_data_buffer, received);

    // The hold-open loop never reads, so nothing should have come back up the recv port
    ASSERT_from_recv_SIZE(0);

    this->component.stop();
    ASSERT_EQ(this->component.join(), Os::Task::Status::OP_OK);
    peer.close(peerDescriptor);
    ASSERT_from_ready_SIZE(1);
}

void UnifiedByteStreamDriverTester::test_buffer_deallocation() {
    U8 data[1] = {};
    Fw::Buffer buffer(data, sizeof(data));
    this->invoke_to_recvReturnIn(0, buffer);
    ASSERT_from_deallocate_SIZE(1);
    ASSERT_EQ(this->fromPortHistory_deallocate->at(0).fwBuffer.getData(), data);
    ASSERT_EQ(this->fromPortHistory_deallocate->at(0).fwBuffer.getSize(), sizeof(data));
}

void UnifiedByteStreamDriverTester::test_telemetry() {
    this->setParameters(ByteStreamTransport::UDP, loopback(50014), unset());
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::UDP);

    this->invoke_to_run(0, 0);
    ASSERT_TLM_Transport_SIZE(1);
    ASSERT_TLM_Transport(0, ByteStreamTransport::UDP);
    ASSERT_TLM_BytesSent_SIZE(1);
    ASSERT_TLM_BytesSent(0, 0);
    ASSERT_TLM_BytesRecv_SIZE(1);
    ASSERT_TLM_BytesRecv(0, 0);
    ASSERT_TLM_Connected_SIZE(1);
    ASSERT_TLM_Connected(0, false);
}

// ----------------------------------------------------------------------
// Handlers for typed from ports
// ----------------------------------------------------------------------

void UnifiedByteStreamDriverTester::from_recv_handler(const FwIndexType portNum,
                                                      Fw::Buffer& recvBuffer,
                                                      const ByteStreamStatus& recvStatus) {
    this->pushFromPortEntry_recv(recvBuffer, recvStatus);
    if ((recvStatus == ByteStreamStatus::OP_OK) && (recvBuffer.getSize() > 0)) {
        Os::ScopeLock lock(this->m_buffer_lock);
        EXPECT_EQ(this->m_data_buffer.getSize(), recvBuffer.getSize()) << "Invalid transmission size";
        Drv::Test::validate_random_buffer(this->m_data_buffer, recvBuffer.getData());
        this->m_received = true;
    }
    delete[] recvBuffer.getData();
}

Fw::Buffer UnifiedByteStreamDriverTester::from_allocate_handler(const FwIndexType portNum, FwSizeType size) {
    this->pushFromPortEntry_allocate(size);
    return Fw::Buffer(new U8[size], size);
}

}  // end namespace Drv
