// ----------------------------------------------------------------------
// TestMain.cpp
// ----------------------------------------------------------------------

#include "UnifiedByteStreamDriverTester.hpp"

TEST(Configuration, TransportNone) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_transport_none();
}

TEST(Configuration, TcpConnect) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_tcp_connect_configuration();
}

TEST(Configuration, TcpListen) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_tcp_listen_configuration();
}

TEST(Configuration, TcpBothEndpointsRejected) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_tcp_both_endpoints_rejected();
}

TEST(Configuration, TcpNoEndpointRejected) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_tcp_no_endpoint_rejected();
}

TEST(Configuration, UdpBidirectional) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_udp_bidirectional_configuration();
}

TEST(Configuration, UdpReceiveOnly) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_udp_receive_only_configuration();
}

TEST(Configuration, UdpSendOnly) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_udp_send_only_configuration();
}

TEST(Configuration, UdpNoEndpointRejected) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_udp_no_endpoint_rejected();
}

TEST(Configuration, Serial) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_serial_configuration();
}

TEST(Configuration, SerialMissingDeviceRejected) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_serial_missing_device_rejected();
}

TEST(Configuration, SerialIgnoresIpParameters) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_serial_ignores_ip_parameters();
}

TEST(Configuration, IpIgnoresSerialParameters) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_ip_ignores_serial_parameters();
}

TEST(Configuration, InvalidBufferSizeRejected) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_invalid_buffer_size_rejected();
}

TEST(Configuration, InvalidSendTimeoutRejected) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_invalid_send_timeout_rejected();
}

TEST(Configuration, ChangeDeferred) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_configuration_change_deferred();
}

TEST(Configuration, UnconfiguredDriverRefusesSend) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_unconfigured_driver_refuses_send();
}

TEST(Configuration, WildcardAddressIsSet) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_wildcard_address_is_set();
}

TEST(Configuration, WildcardLocalPortIsSet) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_wildcard_local_port_is_set();
}

TEST(Configuration, TcpWildcardRemotePortRejected) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_tcp_wildcard_remote_port_rejected();
}

TEST(Configuration, UdpReplyToSenderNeedsLocal) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_udp_reply_to_sender_needs_local();
}

TEST(Nominal, EphemeralPortReported) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_ephemeral_port_reported();
}

TEST(Nominal, UdpMessaging) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_udp_messaging();
}

TEST(Nominal, TcpConnectMessaging) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_tcp_connect_messaging();
}

TEST(Nominal, TcpListenMessaging) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_tcp_listen_messaging();
}

TEST(Nominal, SerialMessaging) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_serial_messaging();
}

TEST(Nominal, BufferDeallocation) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_buffer_deallocation();
}

TEST(Nominal, Telemetry) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_telemetry();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
