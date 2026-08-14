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
//! Address used by every test that needs a routable endpoint
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

void UnifiedByteStreamDriverTester::setParameters(const ByteStreamTransport transport,
                                                  const char* const localAddress,
                                                  const U16 localPort,
                                                  const char* const remoteAddress,
                                                  const U16 remotePort,
                                                  const char* const serialDevice) {
    Fw::String value;
    this->paramSet_TRANSPORT(transport, Fw::ParamValid::VALID);
    value = localAddress;
    this->paramSet_LOCAL_ADDRESS(value, Fw::ParamValid::VALID);
    this->paramSet_LOCAL_PORT(localPort, Fw::ParamValid::VALID);
    value = remoteAddress;
    this->paramSet_REMOTE_ADDRESS(value, Fw::ParamValid::VALID);
    this->paramSet_REMOTE_PORT(remotePort, Fw::ParamValid::VALID);
    value = serialDevice;
    this->paramSet_SERIAL_DEVICE(value, Fw::ParamValid::VALID);
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

ByteStreamDriverMode UnifiedByteStreamDriverTester::loadAndConfigure() {
    // loadParameters pulls every parameter over the prmGet port and then calls
    // parametersLoaded, which is where the driver resolves its configuration
    this->component.loadParameters();
    return this->component.getMode();
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
    this->setParameters(ByteStreamTransport::NONE, "", 0, "", 0);
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamDriverMode::DISABLED);
    ASSERT_EVENTS_TransportNotConfigured_SIZE(1);
    ASSERT_EVENTS_ConfigurationApplied_SIZE(0);
}

void UnifiedByteStreamDriverTester::test_tcp_client_configuration() {
    this->setParameters(ByteStreamTransport::TCP, "", 0, LOOPBACK, 50000);
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamDriverMode::TCP_CLIENT);
    ASSERT_EVENTS_ConfigurationApplied_SIZE(1);
    ASSERT_EVENTS_ConfigurationApplied(0, ByteStreamDriverMode::TCP_CLIENT, "127.0.0.1:50000");
    ASSERT_EVENTS_UnsupportedConfiguration_SIZE(0);
    // A TCP client has no local port of its own to report
    ASSERT_EQ(this->component.getLocalPort(), 0);
}

void UnifiedByteStreamDriverTester::test_tcp_server_configuration() {
    this->setParameters(ByteStreamTransport::TCP, LOOPBACK, 50001, "", 0);
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamDriverMode::TCP_SERVER);
    ASSERT_EVENTS_ConfigurationApplied_SIZE(1);
    ASSERT_EVENTS_ConfigurationApplied(0, ByteStreamDriverMode::TCP_SERVER, "127.0.0.1:50001");
    ASSERT_EVENTS_UnsupportedConfiguration_SIZE(0);
}

void UnifiedByteStreamDriverTester::test_tcp_both_endpoints_rejected() {
    this->setParameters(ByteStreamTransport::TCP, LOOPBACK, 50002, LOOPBACK, 50003);
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamDriverMode::DISABLED);
    ASSERT_EVENTS_UnsupportedConfiguration_SIZE(1);
    ASSERT_EVENTS_UnsupportedConfiguration(0, ByteStreamTransport::TCP, ByteStreamConfigError::TCP_LOCAL_AND_REMOTE);
    ASSERT_EVENTS_ConfigurationApplied_SIZE(0);
}

void UnifiedByteStreamDriverTester::test_tcp_no_endpoint_rejected() {
    this->setParameters(ByteStreamTransport::TCP, "", 0, "", 0);
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamDriverMode::DISABLED);
    ASSERT_EVENTS_UnsupportedConfiguration_SIZE(1);
    ASSERT_EVENTS_UnsupportedConfiguration(0, ByteStreamTransport::TCP, ByteStreamConfigError::NO_ENDPOINT);
}

void UnifiedByteStreamDriverTester::test_tcp_missing_remote_port_rejected() {
    this->setParameters(ByteStreamTransport::TCP, "", 0, LOOPBACK, 0);
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamDriverMode::DISABLED);
    ASSERT_EVENTS_UnsupportedConfiguration_SIZE(1);
    ASSERT_EVENTS_UnsupportedConfiguration(0, ByteStreamTransport::TCP, ByteStreamConfigError::MISSING_REMOTE_PORT);
}

void UnifiedByteStreamDriverTester::test_non_dotted_quad_rejected() {
    this->setParameters(ByteStreamTransport::TCP, "", 0, "localhost", 50004);
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamDriverMode::DISABLED);
    ASSERT_EVENTS_UnsupportedConfiguration_SIZE(1);
    ASSERT_EVENTS_UnsupportedConfiguration(0, ByteStreamTransport::TCP, ByteStreamConfigError::INVALID_REMOTE_ADDRESS);
}

void UnifiedByteStreamDriverTester::test_udp_bidirectional_configuration() {
    this->setParameters(ByteStreamTransport::UDP, LOOPBACK, 50005, LOOPBACK, 50006);
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamDriverMode::UDP);
    ASSERT_EVENTS_ConfigurationApplied_SIZE(1);
    ASSERT_EVENTS_ConfigurationApplied(0, ByteStreamDriverMode::UDP, "127.0.0.1:50005 -> 127.0.0.1:50006");
}

void UnifiedByteStreamDriverTester::test_udp_receive_only_configuration() {
    this->setParameters(ByteStreamTransport::UDP, LOOPBACK, 50007, "", 0);
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamDriverMode::UDP);
    ASSERT_EVENTS_ConfigurationApplied_SIZE(1);
    ASSERT_EVENTS_ConfigurationApplied(0, ByteStreamDriverMode::UDP, "127.0.0.1:50007");
    ASSERT_TRUE(this->component.m_receiveEnabled);
}

void UnifiedByteStreamDriverTester::test_udp_send_only_configuration() {
    this->setParameters(ByteStreamTransport::UDP, "", 0, LOOPBACK, 50008);
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamDriverMode::UDP);
    ASSERT_EVENTS_ConfigurationApplied_SIZE(1);
    ASSERT_EVENTS_ConfigurationApplied(0, ByteStreamDriverMode::UDP, "127.0.0.1:50008");
    // Nothing is bound locally, so there is no receive direction to read
    ASSERT_FALSE(this->component.m_receiveEnabled);
}

void UnifiedByteStreamDriverTester::test_udp_no_endpoint_rejected() {
    this->setParameters(ByteStreamTransport::UDP, "", 0, "", 0);
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamDriverMode::DISABLED);
    ASSERT_EVENTS_UnsupportedConfiguration_SIZE(1);
    ASSERT_EVENTS_UnsupportedConfiguration(0, ByteStreamTransport::UDP, ByteStreamConfigError::NO_ENDPOINT);
}

void UnifiedByteStreamDriverTester::test_serial_configuration() {
    this->setParameters(ByteStreamTransport::SERIAL, "", 0, "", 0, TEST_DEVICE);
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamDriverMode::SERIAL);
    ASSERT_EVENTS_ConfigurationApplied_SIZE(1);
    ASSERT_EVENTS_ConfigurationApplied(0, ByteStreamDriverMode::SERIAL, "/dev/ttyUSB0 @ 115200");
    ASSERT_EVENTS_IgnoredConfiguration_SIZE(0);
}

void UnifiedByteStreamDriverTester::test_serial_missing_device_rejected() {
    this->setParameters(ByteStreamTransport::SERIAL, "", 0, "", 0, "");
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamDriverMode::DISABLED);
    ASSERT_EVENTS_UnsupportedConfiguration_SIZE(1);
    ASSERT_EVENTS_UnsupportedConfiguration(0, ByteStreamTransport::SERIAL, ByteStreamConfigError::NO_SERIAL_DEVICE);
}

void UnifiedByteStreamDriverTester::test_serial_ignores_ip_parameters() {
    this->setParameters(ByteStreamTransport::SERIAL, LOOPBACK, 50009, "", 0, TEST_DEVICE);
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamDriverMode::SERIAL);
    ASSERT_EVENTS_IgnoredConfiguration_SIZE(1);
    ASSERT_EVENTS_IgnoredConfiguration(0, ByteStreamConfigGroup::IP, ByteStreamTransport::SERIAL);
    // Ignoring parameters is a warning, not a rejection: the serial link still comes up
    ASSERT_EVENTS_ConfigurationApplied_SIZE(1);
}

void UnifiedByteStreamDriverTester::test_ip_ignores_serial_parameters() {
    this->setParameters(ByteStreamTransport::UDP, LOOPBACK, 50010, "", 0, TEST_DEVICE);
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamDriverMode::UDP);
    ASSERT_EVENTS_IgnoredConfiguration_SIZE(1);
    ASSERT_EVENTS_IgnoredConfiguration(0, ByteStreamConfigGroup::SERIAL, ByteStreamTransport::UDP);
    ASSERT_EVENTS_ConfigurationApplied_SIZE(1);
}

void UnifiedByteStreamDriverTester::test_invalid_buffer_size_rejected() {
    this->setParameters(ByteStreamTransport::UDP, LOOPBACK, 50011, "", 0);
    const FwSizeType zero = 0;
    this->paramSet_RECV_BUFFER_SIZE(zero, Fw::ParamValid::VALID);
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamDriverMode::DISABLED);
    ASSERT_EVENTS_UnsupportedConfiguration_SIZE(1);
    ASSERT_EVENTS_UnsupportedConfiguration(0, ByteStreamTransport::UDP, ByteStreamConfigError::INVALID_BUFFER_SIZE);
}

void UnifiedByteStreamDriverTester::test_invalid_send_timeout_rejected() {
    this->setParameters(ByteStreamTransport::UDP, LOOPBACK, 50012, "", 0);
    const U32 tooLarge = 1000000;
    this->paramSet_SEND_TIMEOUT_MICROSECONDS(tooLarge, Fw::ParamValid::VALID);
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamDriverMode::DISABLED);
    ASSERT_EVENTS_UnsupportedConfiguration_SIZE(1);
    ASSERT_EVENTS_UnsupportedConfiguration(0, ByteStreamTransport::UDP, ByteStreamConfigError::INVALID_SEND_TIMEOUT);
}

void UnifiedByteStreamDriverTester::test_configuration_change_deferred() {
    this->setParameters(ByteStreamTransport::UDP, LOOPBACK, 50013, "", 0);
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamDriverMode::UDP);
    ASSERT_EVENTS_ConfigurationChangeDeferred_SIZE(0);

    // A second resolution must not disturb the configuration already in place
    this->setParameters(ByteStreamTransport::SERIAL, "", 0, "", 0, TEST_DEVICE);
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamDriverMode::UDP);
    ASSERT_EVENTS_ConfigurationChangeDeferred_SIZE(1);
    ASSERT_EVENTS_ConfigurationApplied_SIZE(1);
}

void UnifiedByteStreamDriverTester::test_disabled_driver_refuses_send() {
    this->setParameters(ByteStreamTransport::NONE, "", 0, "", 0);
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamDriverMode::DISABLED);

    U8 data[8] = {};
    Fw::Buffer buffer(data, sizeof(data));
    ASSERT_EQ(this->invoke_to_send(0, buffer), ByteStreamStatus::OTHER_ERROR);

    // Starting a disabled driver is a no-op rather than an error
    this->component.start();
    ASSERT_EQ(this->component.join(), Os::Task::Status::OP_OK);
}

void UnifiedByteStreamDriverTester::test_dotted_quad_validation() {
    ASSERT_TRUE(UnifiedByteStreamDriver::isDottedQuadIpv4("0.0.0.0"));
    ASSERT_TRUE(UnifiedByteStreamDriver::isDottedQuadIpv4("127.0.0.1"));
    ASSERT_TRUE(UnifiedByteStreamDriver::isDottedQuadIpv4("255.255.255.255"));
    ASSERT_TRUE(UnifiedByteStreamDriver::isDottedQuadIpv4("192.168.1.10"));

    ASSERT_FALSE(UnifiedByteStreamDriver::isDottedQuadIpv4(""));
    ASSERT_FALSE(UnifiedByteStreamDriver::isDottedQuadIpv4("localhost"));
    ASSERT_FALSE(UnifiedByteStreamDriver::isDottedQuadIpv4("1.2.3"));
    ASSERT_FALSE(UnifiedByteStreamDriver::isDottedQuadIpv4("1.2.3.4.5"));
    ASSERT_FALSE(UnifiedByteStreamDriver::isDottedQuadIpv4("1.2.3."));
    ASSERT_FALSE(UnifiedByteStreamDriver::isDottedQuadIpv4("256.1.1.1"));
    ASSERT_FALSE(UnifiedByteStreamDriver::isDottedQuadIpv4("1.1.1.1234"));
    ASSERT_FALSE(UnifiedByteStreamDriver::isDottedQuadIpv4("1.1.1.-1"));
    ASSERT_FALSE(UnifiedByteStreamDriver::isDottedQuadIpv4("1.1.1.1 "));
    // Rejected for the same reason inet_pton rejects it: a leading zero is octal notation
    // to some parsers and would make the address ambiguous
    ASSERT_FALSE(UnifiedByteStreamDriver::isDottedQuadIpv4("010.1.1.1"));
    ASSERT_FALSE(UnifiedByteStreamDriver::isDottedQuadIpv4("::1"));
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

    this->setParameters(ByteStreamTransport::UDP, LOOPBACK, driverPort, LOOPBACK, peerPort);
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamDriverMode::UDP);

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

void UnifiedByteStreamDriverTester::test_tcp_client_messaging() {
    const U16 port = Drv::Test::get_free_port();
    ASSERT_NE(port, 0);

    // Bring the listener up before the driver starts connecting to it
    Drv::TcpServerSocket peer;
    Drv::SocketDescriptor peerDescriptor;
    ASSERT_EQ(peer.configure(LOOPBACK, port, 0, 100), Drv::SOCK_SUCCESS);
    ASSERT_EQ(peer.startup(peerDescriptor), Drv::SOCK_SUCCESS);

    this->setParameters(ByteStreamTransport::TCP, "", 0, LOOPBACK, port);
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamDriverMode::TCP_CLIENT);

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

void UnifiedByteStreamDriverTester::test_tcp_server_messaging() {
    const U16 port = Drv::Test::get_free_port();
    ASSERT_NE(port, 0);

    this->setParameters(ByteStreamTransport::TCP, LOOPBACK, port, "", 0);
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamDriverMode::TCP_SERVER);
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

    this->setParameters(ByteStreamTransport::SERIAL, "", 0, "", 0, device);
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamDriverMode::SERIAL);

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

void UnifiedByteStreamDriverTester::test_buffer_deallocation() {
    U8 data[1] = {};
    Fw::Buffer buffer(data, sizeof(data));
    this->invoke_to_recvReturnIn(0, buffer);
    ASSERT_from_deallocate_SIZE(1);
    ASSERT_EQ(this->fromPortHistory_deallocate->at(0).fwBuffer.getData(), data);
    ASSERT_EQ(this->fromPortHistory_deallocate->at(0).fwBuffer.getSize(), sizeof(data));
}

void UnifiedByteStreamDriverTester::test_telemetry() {
    this->setParameters(ByteStreamTransport::UDP, LOOPBACK, 50014, "", 0);
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamDriverMode::UDP);

    this->invoke_to_run(0, 0);
    ASSERT_TLM_Mode_SIZE(1);
    ASSERT_TLM_Mode(0, ByteStreamDriverMode::UDP);
    ASSERT_TLM_BytesSent_SIZE(1);
    ASSERT_TLM_BytesSent(0, 0);
    ASSERT_TLM_BytesReceived_SIZE(1);
    ASSERT_TLM_BytesReceived(0, 0);
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
