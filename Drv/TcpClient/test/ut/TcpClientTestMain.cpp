// ----------------------------------------------------------------------
// TestMain.cpp
// ----------------------------------------------------------------------

#include "TcpClientTester.hpp"

#include <netinet/in.h>
#include <sys/socket.h>
#include <Drv/Ip/TcpClientSocket.hpp>
#include <Drv/Ip/TcpServerSocket.hpp>
#include <Drv/Ip/test/ut/PortSelector.hpp>
#include <cstring>

TEST(Nominal, TcpClientBasicMessaging) {
    Drv::TcpClientTester tester;
    tester.test_basic_messaging();
}

TEST(Nominal, TcpClientBasicReceiveThread) {
    Drv::TcpClientTester tester;
    tester.test_receive_thread();
}

TEST(Nominal, TcpClientBufferDeallocation) {
    Drv::TcpClientTester tester;
    tester.test_buffer_deallocation();
}

TEST(Reconnect, TcpClientMultiMessaging) {
    Drv::TcpClientTester tester;
    tester.test_multiple_messaging();
}

TEST(Reconnect, TcpClientReceiveThreadReconnect) {
    Drv::TcpClientTester tester;
    tester.test_advanced_reconnect();
}

TEST(AutoConnect, AutoConnectOnSendOff) {
    Drv::TcpClientTester tester;
    tester.test_no_automatic_send_connection();
}

TEST(AutoConnect, AutoConnectOnRecvOff) {
    Drv::TcpClientTester tester;
    tester.test_no_automatic_recv_connection();
}

// A local endpoint is a socket-level setting, so it is exercised at that level rather than
// through the component
TEST(LocalBind, ConnectsFromTheConfiguredLocalEndpoint) {
    Drv::TcpServerSocket server;
    Drv::SocketDescriptor serverDescriptor;
    ASSERT_EQ(server.configure("127.0.0.1", 0, 0, 100), Drv::SOCK_SUCCESS);
    ASSERT_EQ(server.startup(serverDescriptor), Drv::SOCK_SUCCESS);

    const U16 sourcePort = Drv::Test::get_free_port();
    ASSERT_NE(sourcePort, 0);

    Drv::TcpClientSocket client;
    Drv::SocketDescriptor clientDescriptor;
    ASSERT_EQ(client.configure("127.0.0.1", server.getListenPort(), 0, 100), Drv::SOCK_SUCCESS);
    ASSERT_EQ(client.configureLocal("127.0.0.1", sourcePort), Drv::SOCK_SUCCESS);
    ASSERT_EQ(client.open(clientDescriptor), Drv::SOCK_SUCCESS);
    ASSERT_EQ(server.open(serverDescriptor), Drv::SOCK_SUCCESS);

    // Ask the accepted socket where the connection came from
    struct sockaddr_in source;
    socklen_t sourceSize = sizeof(source);
    (void)::memset(&source, 0, sizeof(source));
    ASSERT_EQ(::getpeername(serverDescriptor.fd, reinterpret_cast<struct sockaddr*>(&source), &sourceSize), 0);
    ASSERT_EQ(ntohs(source.sin_port), sourcePort) << "Client did not connect from the local endpoint it was given";

    client.close(clientDescriptor);
    server.terminate(serverDescriptor);
}

// With no local endpoint configured the client behaves exactly as it always has
TEST(LocalBind, WithoutALocalEndpointNothingChanges) {
    Drv::TcpServerSocket server;
    Drv::SocketDescriptor serverDescriptor;
    ASSERT_EQ(server.configure("127.0.0.1", 0, 0, 100), Drv::SOCK_SUCCESS);
    ASSERT_EQ(server.startup(serverDescriptor), Drv::SOCK_SUCCESS);

    Drv::TcpClientSocket client;
    Drv::SocketDescriptor clientDescriptor;
    ASSERT_EQ(client.configure("127.0.0.1", server.getListenPort(), 0, 100), Drv::SOCK_SUCCESS);
    ASSERT_EQ(client.open(clientDescriptor), Drv::SOCK_SUCCESS);
    ASSERT_EQ(server.open(serverDescriptor), Drv::SOCK_SUCCESS);

    // The system still picked a source port of its own
    struct sockaddr_in source;
    socklen_t sourceSize = sizeof(source);
    (void)::memset(&source, 0, sizeof(source));
    ASSERT_EQ(::getpeername(serverDescriptor.fd, reinterpret_cast<struct sockaddr*>(&source), &sourceSize), 0);
    ASSERT_NE(ntohs(source.sin_port), 0);

    client.close(clientDescriptor);
    server.terminate(serverDescriptor);
}

// An all-zero local endpoint is the wildcard, which is what the system would have chosen
TEST(LocalBind, WildcardLocalEndpointBindsEphemeral) {
    Drv::TcpServerSocket server;
    Drv::SocketDescriptor serverDescriptor;
    ASSERT_EQ(server.configure("127.0.0.1", 0, 0, 100), Drv::SOCK_SUCCESS);
    ASSERT_EQ(server.startup(serverDescriptor), Drv::SOCK_SUCCESS);

    Drv::TcpClientSocket client;
    Drv::SocketDescriptor clientDescriptor;
    ASSERT_EQ(client.configure("127.0.0.1", server.getListenPort(), 0, 100), Drv::SOCK_SUCCESS);
    ASSERT_EQ(client.configureLocal("0.0.0.0", 0), Drv::SOCK_SUCCESS);
    ASSERT_EQ(client.open(clientDescriptor), Drv::SOCK_SUCCESS);
    ASSERT_EQ(server.open(serverDescriptor), Drv::SOCK_SUCCESS);

    client.close(clientDescriptor);
    server.terminate(serverDescriptor);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
