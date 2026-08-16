// ======================================================================
// \title  UnifiedByteStreamDriverTester.cpp
// \brief  cpp file for UnifiedByteStreamDriver test harness implementation class
// ======================================================================

#include "UnifiedByteStreamDriverTester.hpp"

#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
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
//! Read timeout the tests configure, in tenths of a second
const U8 TEST_READ_TIMEOUT = 1;
}  // namespace

// ----------------------------------------------------------------------
// Construction and destruction
// ----------------------------------------------------------------------

UnifiedByteStreamDriverTester::UnifiedByteStreamDriverTester()
    : UnifiedByteStreamDriverGTestBase("Tester", MAX_HISTORY_SIZE),
      component("UnifiedByteStreamDriver"),
      m_data_buffer(m_data_storage, sizeof(m_data_storage)),
      m_received(false),
      m_starve_allocator(false),
      m_outstanding_buffers(0) {
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
    this->paramSet_SERIAL_CONFIG(UnifiedByteStreamDriverTester::serial(serialDevice), Fw::ParamValid::VALID);
    const FwSizeType bufferSize = sizeof(this->m_data_storage);
    this->paramSet_RECV_BUFFER_SIZE(bufferSize, Fw::ParamValid::VALID);
    SendTimeout timeout;
    timeout.set_seconds(0);
    timeout.set_microseconds(100);
    this->paramSet_SEND_TIMEOUT(timeout, Fw::ParamValid::VALID);
}

SerialConfig UnifiedByteStreamDriverTester::serial(const char* const device) {
    SerialConfig config;
    config.set_device(Fw::String(device));
    config.set_baudRate(SerialBaudRate::BAUD_115200);
    config.set_parity(SerialParity::PARITY_NONE);
    config.set_flowControl(SerialFlowControl::FLOW_NONE);
    config.set_readTimeout(TEST_READ_TIMEOUT);
    return config;
}

ByteStreamTransport UnifiedByteStreamDriverTester::loadAndConfigure() {
    // loadParameters pulls every parameter over the prmGet port, one parameterUpdated call
    // each, which stage the resolution that configure then does once - the same order
    // start() puts them in
    this->component.loadParameters();
    return this->component.configure();
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

void UnifiedByteStreamDriverTester::test_tcp_client_configuration() {
    this->setParameters(ByteStreamTransport::TCP_CLIENT, unset(), loopback(50000));
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::TCP_CLIENT);
    ASSERT_EVENTS_ConfigurationApplied_SIZE(1);
    ASSERT_EVENTS_ConfigurationApplied(0, ByteStreamTransport::TCP_CLIENT, "connect 127.0.0.1:50000");
    ASSERT_EVENTS_UnsupportedConfiguration_SIZE(0);
}

void UnifiedByteStreamDriverTester::test_tcp_client_missing_remote_rejected() {
    this->setParameters(ByteStreamTransport::TCP_CLIENT, loopback(50001), unset());
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::NONE);
    ASSERT_EVENTS_UnsupportedConfiguration(0, ByteStreamTransport::TCP_CLIENT,
                                           ByteStreamConfigError::MISSING_REMOTE_ENDPOINT);
}

void UnifiedByteStreamDriverTester::test_tcp_server_configuration() {
    this->setParameters(ByteStreamTransport::TCP_SERVER, loopback(50004), unset());
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::TCP_SERVER);
    ASSERT_EVENTS_ConfigurationApplied(0, ByteStreamTransport::TCP_SERVER, "listen 127.0.0.1:50004");
}

void UnifiedByteStreamDriverTester::test_tcp_server_wildcard_listen() {
    // Nothing supplied at all: a zero local endpoint is a wildcard bind, not a missing one
    this->setParameters(ByteStreamTransport::TCP_SERVER, unset(), unset());
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::TCP_SERVER);
    ASSERT_EVENTS_UnsupportedConfiguration_SIZE(0);
    ASSERT_EVENTS_ConfigurationApplied(0, ByteStreamTransport::TCP_SERVER, "listen 0.0.0.0:0");
}

void UnifiedByteStreamDriverTester::test_udp_bidirectional_configuration() {
    this->setParameters(ByteStreamTransport::UDP, loopback(50005), loopback(50006));
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::UDP);
    ASSERT_EVENTS_ConfigurationApplied(0, ByteStreamTransport::UDP, "bind 127.0.0.1:50005 send 127.0.0.1:50006");
}

void UnifiedByteStreamDriverTester::test_udp_receive_only_configuration() {
    this->setParameters(ByteStreamTransport::UDP, loopback(50007), unset());
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::UDP);
    ASSERT_EVENTS_ConfigurationApplied(0, ByteStreamTransport::UDP, "bind 127.0.0.1:50007");
}

void UnifiedByteStreamDriverTester::test_udp_wildcard_bind() {
    this->setParameters(ByteStreamTransport::UDP, unset(), unset());
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::UDP);
    ASSERT_EVENTS_UnsupportedConfiguration_SIZE(0);
    ASSERT_EVENTS_ConfigurationApplied(0, ByteStreamTransport::UDP, "bind 0.0.0.0:0");
}

void UnifiedByteStreamDriverTester::test_incomplete_remote_rejected() {
    // An address with no port addresses no destination, so it is rejected rather than
    // being taken for "no destination"
    this->setParameters(ByteStreamTransport::TCP_CLIENT, unset(), loopback(0));
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::NONE);
    ASSERT_EVENTS_UnsupportedConfiguration(0, ByteStreamTransport::TCP_CLIENT,
                                           ByteStreamConfigError::INCOMPLETE_REMOTE_ENDPOINT);
}

void UnifiedByteStreamDriverTester::test_remote_port_without_address_rejected() {
    this->setParameters(ByteStreamTransport::UDP, unset(), endpoint(0, 0, 0, 0, 50008));
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::NONE);
    ASSERT_EVENTS_UnsupportedConfiguration(0, ByteStreamTransport::UDP,
                                           ByteStreamConfigError::INCOMPLETE_REMOTE_ENDPOINT);
}

void UnifiedByteStreamDriverTester::test_serial_configuration() {
    this->setParameters(ByteStreamTransport::SERIAL, unset(), unset(), TEST_DEVICE);
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::SERIAL);
    ASSERT_EVENTS_ConfigurationApplied(0, ByteStreamTransport::SERIAL, "/dev/ttyUSB0 @ 115200");
}

void UnifiedByteStreamDriverTester::test_serial_missing_device_rejected() {
    this->setParameters(ByteStreamTransport::SERIAL, unset(), unset(), "");
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::NONE);
    ASSERT_EVENTS_UnsupportedConfiguration(0, ByteStreamTransport::SERIAL, ByteStreamConfigError::NO_SERIAL_DEVICE);
}

void UnifiedByteStreamDriverTester::test_invalid_buffer_size_rejected() {
    this->setParameters(ByteStreamTransport::UDP, loopback(50009), unset());
    const FwSizeType zero = 0;
    this->paramSet_RECV_BUFFER_SIZE(zero, Fw::ParamValid::VALID);
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::NONE);
    ASSERT_EVENTS_UnsupportedConfiguration(0, ByteStreamTransport::UDP, ByteStreamConfigError::INVALID_BUFFER_SIZE);
}

void UnifiedByteStreamDriverTester::test_invalid_send_timeout_rejected() {
    this->setParameters(ByteStreamTransport::UDP, loopback(50010), unset());
    SendTimeout timeout;
    timeout.set_seconds(0);
    timeout.set_microseconds(1000000);
    this->paramSet_SEND_TIMEOUT(timeout, Fw::ParamValid::VALID);
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::NONE);
    ASSERT_EVENTS_UnsupportedConfiguration(0, ByteStreamTransport::UDP, ByteStreamConfigError::INVALID_SEND_TIMEOUT);
}

void UnifiedByteStreamDriverTester::test_unconfigured_driver_refuses_send() {
    this->setParameters(ByteStreamTransport::NONE, unset(), unset());
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::NONE);

    U8 data[8] = {};
    Fw::Buffer buffer(data, sizeof(data));
    ASSERT_EQ(this->invoke_to_send(0, buffer), ByteStreamStatus::OTHER_ERROR);
    // A refused send is an event, not just a line in a text log
    ASSERT_EVENTS_SendError_SIZE(1);

    // Starting an unconfigured driver is a no-op rather than an error
    this->component.start();
    ASSERT_EQ(this->component.join(), Os::Task::Status::OP_OK);
}

void UnifiedByteStreamDriverTester::test_configuration_telemetry() {
    this->setParameters(ByteStreamTransport::UDP, loopback(50011), loopback(50012), TEST_DEVICE);
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::UDP);

    // Every parameter reads back as telemetry, so the configuration in force can be seen
    // from the ground without a parameter dump
    ASSERT_TLM_ActiveTransport(0, ByteStreamTransport::UDP);
    ASSERT_TLM_TRANSPORT(0, ByteStreamTransport::UDP);
    ASSERT_TLM_LOCAL_ENDPOINT(0, loopback(50011));
    ASSERT_TLM_REMOTE_ENDPOINT(0, loopback(50012));
    ASSERT_TLM_SERIAL_CONFIG(0, UnifiedByteStreamDriverTester::serial(TEST_DEVICE));
    ASSERT_TLM_RECV_BUFFER_SIZE(0, sizeof(this->m_data_storage));
}

void UnifiedByteStreamDriverTester::test_direct_configuration() {
    // No parameter database in the picture: the setters write the same storage the
    // parameters use, so configure resolves from them
    SendTimeout timeout;
    timeout.set_seconds(0);
    timeout.set_microseconds(100);
    this->component.setConfiguration(ByteStreamTransport::UDP, loopback(50013), unset());
    this->component.setBufferConfiguration(sizeof(this->m_data_storage), timeout);

    ASSERT_EQ(this->component.configure(), ByteStreamTransport::UDP);
    ASSERT_EVENTS_ConfigurationApplied(0, ByteStreamTransport::UDP, "bind 127.0.0.1:50013");

    // And the same values serialize back out, so a param save would save what was set here
    ASSERT_TLM_LOCAL_ENDPOINT(0, loopback(50013));
}

void UnifiedByteStreamDriverTester::test_command_line_override() {
    // What a deployment does for -a/-p: the parameter database has one endpoint saved, and
    // the command line asks for another.
    this->setParameters(ByteStreamTransport::UDP, loopback(50016), unset());
    this->component.loadParameters();
    this->component.setConfiguration(ByteStreamTransport::UDP, loopback(50017), unset());

    ASSERT_EQ(this->component.configure(), ByteStreamTransport::UDP);
    ASSERT_EVENTS_ConfigurationApplied(0, ByteStreamTransport::UDP, "bind 127.0.0.1:50017");
    ASSERT_TLM_LOCAL_ENDPOINT(0, loopback(50017));
}

void UnifiedByteStreamDriverTester::test_configuration_before_load_survives() {
    // The other order, and the harder case: the database has an endpoint saved, so the load
    // has a real value to write rather than a default. The deployment's value still wins.
    this->setParameters(ByteStreamTransport::UDP, loopback(50018), unset());
    this->component.setConfiguration(ByteStreamTransport::UDP, loopback(50019), unset());
    this->component.loadParameters();

    ASSERT_EQ(this->component.configure(), ByteStreamTransport::UDP);
    ASSERT_EVENTS_ConfigurationApplied(0, ByteStreamTransport::UDP, "bind 127.0.0.1:50019");
    ASSERT_TLM_LOCAL_ENDPOINT(0, loopback(50019));
}

void UnifiedByteStreamDriverTester::test_parameter_update_reconfigures() {
    this->setParameters(ByteStreamTransport::UDP, loopback(50014), unset());
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::UDP);

    // A parameter changed before anything is running still takes effect, without a restart.
    // Each set resolves on its own, so the device has to land before the transport that
    // needs it: SERIAL with no device yet is a configuration this driver refuses.
    this->setParameters(ByteStreamTransport::SERIAL, unset(), unset(), TEST_DEVICE);
    this->paramSend_SERIAL_CONFIG(0, 0);
    this->paramSend_TRANSPORT(0, 0);
    ASSERT_EQ(this->component.getTransport(), ByteStreamTransport::SERIAL);
    ASSERT_EVENTS_ConfigurationReloaded_SIZE(2);
}

void UnifiedByteStreamDriverTester::test_parameter_update_while_running() {
    const U16 first = Drv::Test::get_free_port(true);
    ASSERT_NE(first, 0);
    this->setParameters(ByteStreamTransport::UDP, loopback(first), unset());
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::UDP);
    this->component.start();
    ASSERT_TRUE(this->wait_on_open(true, WAIT_ITERATIONS)) << "Driver never bound its first port";
    ASSERT_EQ(this->component.getLocalPort(), first);

    // Move the link to a different port while it is up. The live connection is dropped and
    // the read task's own reconnect path brings it back on the new value.
    U16 second = Drv::Test::get_free_port(true);
    for (U32 i = 0; (i < 100) && (second == first); i++) {
        second = Drv::Test::get_free_port(true);
    }
    if (second == first) {
        this->component.stop();
        (void)this->component.join();
        GTEST_SKIP() << "Could not find a second free UDP port";
    }
    this->paramSet_LOCAL_ENDPOINT(loopback(second), Fw::ParamValid::VALID);
    this->paramSend_LOCAL_ENDPOINT(0, 0);

    ASSERT_TRUE(this->wait_on_open(true, WAIT_ITERATIONS)) << "Driver never came back after the change";
    // Poll rather than assert once: the read task is what applies the staged change
    U16 moved = 0;
    for (U32 i = 0; (i < WAIT_ITERATIONS) && (moved != second); i++) {
        moved = this->component.getLocalPort();
        if (moved != second) {
            (void)Os::Task::delay(Fw::TimeInterval(0, 10000));
        }
    }
    ASSERT_EQ(moved, second) << "Parameter change did not move the link";

    this->component.stop();
    ASSERT_EQ(this->component.join(), Os::Task::Status::OP_OK);

    // The read task logs the reload, so read that history only once it is joined
    ASSERT_EVENTS_ConfigurationReloaded_SIZE(1);
}

void UnifiedByteStreamDriverTester::test_transport_switch_while_running() {
    // Up on UDP first
    const U16 udpPort = Drv::Test::get_free_port(true);
    ASSERT_NE(udpPort, 0);
    this->setParameters(ByteStreamTransport::UDP, loopback(udpPort), unset());
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::UDP);
    this->component.start();
    ASSERT_TRUE(this->wait_on_open(true, WAIT_ITERATIONS)) << "Driver never opened its UDP socket";

    // Now move it to a listener, which needs a socket the UDP generation never created
    const U16 tcpPort = Drv::Test::get_free_port();
    ASSERT_NE(tcpPort, 0);
    this->paramSet_LOCAL_ENDPOINT(loopback(tcpPort), Fw::ParamValid::VALID);
    this->paramSet_TRANSPORT(ByteStreamTransport::TCP_SERVER, Fw::ParamValid::VALID);
    this->paramSend_LOCAL_ENDPOINT(0, 0);
    this->paramSend_TRANSPORT(0, 0);

    // A peer can only connect if the read task really did come back and listen
    Drv::TcpClientSocket peer;
    Drv::SocketDescriptor peerDescriptor;
    ASSERT_EQ(peer.configure(LOOPBACK, tcpPort, 0, 100), Drv::SOCK_SUCCESS);
    Drv::SocketIpStatus status = Drv::SOCK_FAILED_TO_CONNECT;
    for (U32 i = 0; (i < WAIT_ITERATIONS) && (status != Drv::SOCK_SUCCESS); i++) {
        status = peer.open(peerDescriptor);
        if (status != Drv::SOCK_SUCCESS) {
            (void)Os::Task::delay(Fw::TimeInterval(0, 10000));
        }
    }
    ASSERT_EQ(status, Drv::SOCK_SUCCESS) << "Driver never listened after the switch to TCP_SERVER";
    ASSERT_TRUE(this->wait_on_open(true, WAIT_ITERATIONS)) << "Driver never accepted the connection";
    ASSERT_EQ(this->component.getTransport(), ByteStreamTransport::TCP_SERVER);
    ASSERT_EQ(this->component.getLocalPort(), tcpPort);

    // And it carries data on the transport it moved to
    Drv::Test::force_recv_timeout(this->component.m_descriptor.fd, this->component.getSocketHandler());
    Drv::Test::force_recv_timeout(peerDescriptor.fd, peer);
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

    this->component.stop();
    ASSERT_EQ(this->component.join(), Os::Task::Status::OP_OK);
    peer.close(peerDescriptor);
}

void UnifiedByteStreamDriverTester::test_transport_switch_releases_listener() {
    // The other direction: a listener has to be given up, not left bound, when the transport
    // moves off it. Binding the same port afterwards is what proves it was released.
    const U16 tcpPort = Drv::Test::get_free_port();
    ASSERT_NE(tcpPort, 0);
    this->setParameters(ByteStreamTransport::TCP_SERVER, loopback(tcpPort), unset());
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::TCP_SERVER);
    this->component.start();

    Drv::TcpClientSocket peer;
    Drv::SocketDescriptor peerDescriptor;
    ASSERT_EQ(peer.configure(LOOPBACK, tcpPort, 0, 100), Drv::SOCK_SUCCESS);
    Drv::SocketIpStatus status = Drv::SOCK_FAILED_TO_CONNECT;
    for (U32 i = 0; (i < WAIT_ITERATIONS) && (status != Drv::SOCK_SUCCESS); i++) {
        status = peer.open(peerDescriptor);
        if (status != Drv::SOCK_SUCCESS) {
            (void)Os::Task::delay(Fw::TimeInterval(0, 10000));
        }
    }
    ASSERT_EQ(status, Drv::SOCK_SUCCESS) << "Peer never connected to the driver's listener";
    peer.close(peerDescriptor);

    const U16 udpPort = Drv::Test::get_free_port(true);
    ASSERT_NE(udpPort, 0);
    this->paramSet_LOCAL_ENDPOINT(loopback(udpPort), Fw::ParamValid::VALID);
    this->paramSet_TRANSPORT(ByteStreamTransport::UDP, Fw::ParamValid::VALID);
    this->paramSend_LOCAL_ENDPOINT(0, 0);
    this->paramSend_TRANSPORT(0, 0);

    U16 moved = 0;
    for (U32 i = 0; (i < WAIT_ITERATIONS) && (moved != udpPort); i++) {
        moved = this->component.getLocalPort();
        if (moved != udpPort) {
            (void)Os::Task::delay(Fw::TimeInterval(0, 10000));
        }
    }
    ASSERT_EQ(moved, udpPort) << "Driver never came up on UDP after leaving TCP_SERVER";

    // The listening port must be free again, or the listener outlived its transport
    Drv::TcpServerSocket rebind;
    Drv::SocketDescriptor rebindDescriptor;
    ASSERT_EQ(rebind.configure(LOOPBACK, tcpPort, 0, 100), Drv::SOCK_SUCCESS);
    ASSERT_EQ(rebind.startup(rebindDescriptor), Drv::SOCK_SUCCESS) << "Listening socket was never released";
    rebind.terminate(rebindDescriptor);

    this->component.stop();
    ASSERT_EQ(this->component.join(), Os::Task::Status::OP_OK);
}

void UnifiedByteStreamDriverTester::test_ephemeral_port_reported() {
    this->setParameters(ByteStreamTransport::UDP, loopback(0), unset());
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::UDP);
    ASSERT_EQ(this->component.getLocalPort(), 0);  // nothing assigned until the socket opens

    this->component.start();
    ASSERT_TRUE(this->wait_on_open(true, WAIT_ITERATIONS)) << "Driver never bound its UDP socket";
    const U16 assigned = this->component.getLocalPort();
    ASSERT_NE(assigned, 0) << "Ephemeral port was never assigned";

    // The read task is what logs PortOpened and writes LocalPort, and neither history is
    // synchronized, so join before reading them
    this->component.stop();
    ASSERT_EQ(this->component.join(), Os::Task::Status::OP_OK);

    // PortOpened carries the assigned port, not the zero that was asked for, and the same
    // port is on the LocalPort channel for a ground system to read
    Fw::String expected;
    (void)expected.format("bind 127.0.0.1:%hu", assigned);
    ASSERT_EVENTS_PortOpened(0, ByteStreamTransport::UDP, expected.toChar());
    ASSERT_TLM_LocalPort(1, assigned);
}

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

void UnifiedByteStreamDriverTester::test_tcp_client_messaging() {
    const U16 port = Drv::Test::get_free_port();
    ASSERT_NE(port, 0);

    // Bring the listener up before the driver starts connecting to it
    Drv::TcpServerSocket peer;
    Drv::SocketDescriptor peerDescriptor;
    ASSERT_EQ(peer.configure(LOOPBACK, port, 0, 100), Drv::SOCK_SUCCESS);
    ASSERT_EQ(peer.startup(peerDescriptor), Drv::SOCK_SUCCESS);

    this->setParameters(ByteStreamTransport::TCP_CLIENT, unset(), loopback(port));
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::TCP_CLIENT);

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

    this->setParameters(ByteStreamTransport::TCP_SERVER, loopback(port), unset());
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::TCP_SERVER);
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

void UnifiedByteStreamDriverTester::test_udp_remote_only_messaging() {
    const U16 peerPort = Drv::Test::get_free_port(true);
    ASSERT_NE(peerPort, 0);

    // A destination and nothing else. The wildcard bind that comes with it is what lets the
    // peer's reply arrive, on a port neither side named ahead of time.
    this->setParameters(ByteStreamTransport::UDP, unset(), loopback(peerPort));
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::UDP);

    Drv::UdpSocket peer;
    Drv::SocketDescriptor peerDescriptor;
    ASSERT_EQ(peer.configureRecv(LOOPBACK, peerPort), Drv::SOCK_SUCCESS);
    ASSERT_EQ(peer.open(peerDescriptor), Drv::SOCK_SUCCESS);

    this->component.start();
    ASSERT_TRUE(this->wait_on_open(true, WAIT_ITERATIONS)) << "Driver never opened its UDP socket";
    Drv::Test::force_recv_timeout(this->component.m_descriptor.fd, this->component.getSocketHandler());
    Drv::Test::force_recv_timeout(peerDescriptor.fd, peer);
    ASSERT_NE(this->component.getLocalPort(), 0) << "Wildcard bind never took an ephemeral port";

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

    // Reply to the source of that datagram, which is the driver's ephemeral port
    this->m_received = false;
    {
        Os::ScopeLock lock(this->m_buffer_lock);
        this->m_data_buffer.setSize(sizeof(this->m_data_storage));
        (void)Drv::Test::fill_random_buffer(this->m_data_buffer);
        ASSERT_EQ(peer.send(peerDescriptor, this->m_data_buffer.getData(), this->m_data_buffer.getSize()),
                  Drv::SOCK_SUCCESS);
    }
    ASSERT_TRUE(this->wait_on_receive(WAIT_ITERATIONS)) << "Driver never received the peer's reply";

    this->component.stop();
    ASSERT_EQ(this->component.join(), Os::Task::Status::OP_OK);
    peer.close(peerDescriptor);
    ASSERT_from_ready_SIZE(1);
}

void UnifiedByteStreamDriverTester::test_buffer_deallocation() {
    // Allocated the way the tester's allocator allocates, since the deallocate handler is
    // what gives it back
    const FwSizeType size = 1;
    U8* const data = new U8[size];
    this->m_outstanding_buffers++;
    Fw::Buffer buffer(data, size);
    this->invoke_to_recvReturnIn(0, buffer);
    ASSERT_from_deallocate_SIZE(1);
    ASSERT_EQ(this->fromPortHistory_deallocate->at(0).fwBuffer.getData(), data);
    ASSERT_EQ(this->fromPortHistory_deallocate->at(0).fwBuffer.getSize(), size);
    ASSERT_EQ(this->m_outstanding_buffers.load(), 0);
}

void UnifiedByteStreamDriverTester::test_failed_allocation_returns_buffer() {
    this->setParameters(ByteStreamTransport::UDP, loopback(50015), unset());
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::UDP);

    // The allocator answers with memory it cannot size, which is not a buffer the driver
    // can read into but is still memory the allocator is owed back
    this->m_starve_allocator = true;
    const Fw::Buffer buffer = this->component.getBuffer();
    ASSERT_FALSE(buffer.isValid()) << "A buffer that cannot be read into must not be passed on";
    // The unusable buffer must have gone back to the allocator rather than been dropped,
    // and the ground has to be able to see that it happened
    ASSERT_from_deallocate_SIZE(1);
    ASSERT_EVENTS_NoBuffers_SIZE(1);
    ASSERT_EQ(this->m_outstanding_buffers.load(), 0) << "Allocation leaked";
}

void UnifiedByteStreamDriverTester::test_repeated_failure_throttled() {
    this->setParameters(ByteStreamTransport::UDP, loopback(50016), unset());
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::UDP);

    // An allocator that stays empty fails every read the task attempts, which is exactly the
    // repetition a plain count throttle would answer by going quiet for good. The event
    // throttles on a time window instead, so the reports inside that window are dropped and
    // the window is what lets reporting resume.
    this->m_starve_allocator = true;
    for (U32 i = 0; i < 5; i++) {
        const Fw::Buffer buffer = this->component.getBuffer();
        ASSERT_FALSE(buffer.isValid());
    }
    ASSERT_EVENTS_NoBuffers_SIZE(1);
    ASSERT_EQ(this->m_outstanding_buffers.load(), 0) << "Allocation leaked";
}

void UnifiedByteStreamDriverTester::test_byte_counter_telemetry() {
    const U16 peerPort = Drv::Test::get_free_port(true);
    ASSERT_NE(peerPort, 0);
    this->setParameters(ByteStreamTransport::UDP, unset(), loopback(peerPort));
    ASSERT_EQ(this->loadAndConfigure(), ByteStreamTransport::UDP);

    Drv::UdpSocket peer;
    Drv::SocketDescriptor peerDescriptor;
    ASSERT_EQ(peer.configureRecv(LOOPBACK, peerPort), Drv::SOCK_SUCCESS);
    ASSERT_EQ(peer.open(peerDescriptor), Drv::SOCK_SUCCESS);

    this->component.start();
    ASSERT_TRUE(this->wait_on_open(true, WAIT_ITERATIONS)) << "Driver never opened its UDP socket";

    U8 data[32] = {};
    Fw::Buffer buffer(data, sizeof(data));
    ASSERT_EQ(this->invoke_to_send(0, buffer), ByteStreamStatus::OP_OK);

    this->component.stop();
    ASSERT_EQ(this->component.join(), Os::Task::Status::OP_OK);
    peer.close(peerDescriptor);

    // Pushed as it changes rather than on a rate group tick
    ASSERT_TLM_BytesSent_SIZE(1);
    ASSERT_TLM_BytesSent(0, sizeof(data));
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
    this->m_outstanding_buffers--;
    delete[] recvBuffer.getData();
}

Fw::Buffer UnifiedByteStreamDriverTester::from_allocate_handler(const FwIndexType portNum, FwSizeType size) {
    this->pushFromPortEntry_allocate(size);
    this->m_outstanding_buffers++;
    if (this->m_starve_allocator) {
        // Memory the driver cannot use, but memory all the same
        return Fw::Buffer(new U8[1], 0);
    }
    return Fw::Buffer(new U8[size], size);
}

void UnifiedByteStreamDriverTester::from_deallocate_handler(const FwIndexType portNum, Fw::Buffer& fwBuffer) {
    this->pushFromPortEntry_deallocate(fwBuffer);
    this->m_outstanding_buffers--;
    delete[] fwBuffer.getData();
}

}  // end namespace Drv
