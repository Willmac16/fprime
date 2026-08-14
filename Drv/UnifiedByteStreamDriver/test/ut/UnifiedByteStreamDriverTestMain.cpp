// ----------------------------------------------------------------------
// TestMain.cpp
// ----------------------------------------------------------------------

#include "UnifiedByteStreamDriverTester.hpp"

TEST(Configuration, TransportNone) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_transport_none();
}

TEST(Configuration, TcpClient) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_tcp_client_configuration();
}

TEST(Configuration, TcpServer) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_tcp_server_configuration();
}

TEST(Configuration, TcpBothEndpointsRejected) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_tcp_both_endpoints_rejected();
}

TEST(Configuration, TcpNoEndpointRejected) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_tcp_no_endpoint_rejected();
}

TEST(Configuration, TcpMissingRemotePortRejected) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_tcp_missing_remote_port_rejected();
}

TEST(Configuration, NonDottedQuadRejected) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_non_dotted_quad_rejected();
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

TEST(Configuration, DisabledDriverRefusesSend) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_disabled_driver_refuses_send();
}

TEST(Configuration, DottedQuadValidation) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_dotted_quad_validation();
}

TEST(Nominal, UdpMessaging) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_udp_messaging();
}

TEST(Nominal, TcpClientMessaging) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_tcp_client_messaging();
}

TEST(Nominal, TcpServerMessaging) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_tcp_server_messaging();
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
