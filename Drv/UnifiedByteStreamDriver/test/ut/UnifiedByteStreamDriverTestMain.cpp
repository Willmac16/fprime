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

TEST(Configuration, TcpClientMissingRemoteRejected) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_tcp_client_missing_remote_rejected();
}

TEST(Configuration, TcpServer) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_tcp_server_configuration();
}

TEST(Configuration, TcpServerWildcardListen) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_tcp_server_wildcard_listen();
}

TEST(Configuration, UdpBidirectional) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_udp_bidirectional_configuration();
}

TEST(Configuration, UdpReceiveOnly) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_udp_receive_only_configuration();
}

TEST(Configuration, UdpWildcardBind) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_udp_wildcard_bind();
}

TEST(Configuration, IncompleteRemoteRejected) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_incomplete_remote_rejected();
}

TEST(Configuration, RemotePortWithoutAddressRejected) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_remote_port_without_address_rejected();
}

TEST(Configuration, Serial) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_serial_configuration();
}

TEST(Configuration, SerialMissingDeviceRejected) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_serial_missing_device_rejected();
}

TEST(Configuration, InvalidBufferSizeRejected) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_invalid_buffer_size_rejected();
}

TEST(Configuration, InvalidSendTimeoutRejected) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_invalid_send_timeout_rejected();
}

TEST(Configuration, UnconfiguredDriverRefusesSend) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_unconfigured_driver_refuses_send();
}

TEST(Configuration, ConfigurationTelemetry) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_configuration_telemetry();
}

TEST(Configuration, ParameterTelemetryTracksSetting) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_parameter_telemetry_tracks_setting();
}

TEST(Configuration, ParameterUpdateReconfigures) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_parameter_update_reconfigures();
}

TEST(Nominal, ParameterUpdateWhileRunning) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_parameter_update_while_running();
}

TEST(Nominal, TransportSwitchWhileRunning) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_transport_switch_while_running();
}

TEST(Nominal, TransportSwitchReleasesListener) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_transport_switch_releases_listener();
}

TEST(Nominal, EphemeralPortReported) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_ephemeral_port_reported();
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

TEST(Nominal, UdpRemoteOnlyMessaging) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_udp_remote_only_messaging();
}

TEST(Nominal, BufferDeallocation) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_buffer_deallocation();
}

TEST(Nominal, FailedAllocationReturnsBuffer) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_failed_allocation_returns_buffer();
}

TEST(Nominal, RepeatedFailureThrottled) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_repeated_failure_throttled();
}

TEST(Nominal, ByteCounterTelemetry) {
    Drv::UnifiedByteStreamDriverTester tester;
    tester.test_byte_counter_telemetry();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
