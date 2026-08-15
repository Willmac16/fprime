// ======================================================================
// \title  BindingTcpClientSocket.cpp
// \brief  cpp file for a TCP client socket that can bind a local endpoint
// ======================================================================

#include "BindingTcpClientSocket.hpp"
#include <Fw/Logger/Logger.hpp>
#include <Fw/Types/Assert.hpp>
#include <Fw/Types/StringUtils.hpp>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>

namespace Drv {

BindingTcpClientSocket::BindingTcpClientSocket() : TcpClientSocket() {}

BindingTcpClientSocket::~BindingTcpClientSocket() {}

SocketIpStatus BindingTcpClientSocket::configureLocal(const char* const ipv4_address, const U16 port) {
    FW_ASSERT(ipv4_address != nullptr);
    if (Fw::StringUtils::string_length(ipv4_address, static_cast<FwSizeType>(SOCKET_MAX_IPV4_ADDRESS_SIZE)) >=
        static_cast<FwSizeType>(SOCKET_MAX_IPV4_ADDRESS_SIZE)) {
        return SOCK_INVALID_CALL;
    }
    (void)Fw::StringUtils::string_copy(this->m_local_address, ipv4_address, SOCKET_MAX_IPV4_ADDRESS_SIZE);
    this->m_local_port = port;
    return SOCK_SUCCESS;
}

bool BindingTcpClientSocket::hasLocal() const {
    const bool addressed = (this->m_local_address[0] != '\0') && (::strcmp(this->m_local_address, "0.0.0.0") != 0);
    return addressed || (this->m_local_port != 0);
}

SocketIpStatus BindingTcpClientSocket::openProtocol(SocketDescriptor& socketDescriptor) {
    // Nothing to bind: the base class already does exactly the right thing
    if (not this->hasLocal()) {
        return TcpClientSocket::openProtocol(socketDescriptor);
    }

    struct sockaddr_in local;
    (void)::memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_port = htons(this->m_local_port);
#if defined TGT_OS_TYPE_VXWORKS || TGT_OS_TYPE_DARWIN
    local.sin_len = static_cast<U8>(sizeof(struct sockaddr_in));
#endif
    const SocketIpStatus addressStatus = IpSocket::addressToIp4(this->m_local_address, &local.sin_addr);
    if (addressStatus != SOCK_SUCCESS) {
        return SOCK_INVALID_IP_ADDRESS;
    }

    const int socketFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socketFd == -1) {
        return SOCK_FAILED_TO_GET_SOCKET;
    }
    if (IpSocket::setupSocketOptions(socketFd) != SOCK_SUCCESS) {
        (void)::close(socketFd);
        return SOCK_FAILED_TO_SET_SOCKET_OPTIONS;
    }
    if (IpSocket::setupTimeouts(socketFd) != SOCK_SUCCESS) {
        (void)::close(socketFd);
        return SOCK_FAILED_TO_SET_SOCKET_OPTIONS;
    }
    if (::bind(socketFd, reinterpret_cast<struct sockaddr*>(&local), sizeof(local)) < 0) {
        (void)::close(socketFd);
        return SOCK_FAILED_TO_BIND;
    }

    // The descriptor is bound now, so hand it to the base class connect by way of a plain
    // connect on the address the base class was configured with
    struct sockaddr_in remote;
    (void)::memset(&remote, 0, sizeof(remote));
    remote.sin_family = AF_INET;
    remote.sin_port = htons(this->m_port);
#if defined TGT_OS_TYPE_VXWORKS || TGT_OS_TYPE_DARWIN
    remote.sin_len = static_cast<U8>(sizeof(struct sockaddr_in));
#endif
    if (IpSocket::addressToIp4(this->m_ipv4_address, &remote.sin_addr) != SOCK_SUCCESS) {
        (void)::close(socketFd);
        return SOCK_INVALID_IP_ADDRESS;
    }
    if (::connect(socketFd, reinterpret_cast<struct sockaddr*>(&remote), sizeof(remote)) < 0) {
        (void)::close(socketFd);
        return SOCK_FAILED_TO_CONNECT;
    }

    socketDescriptor.fd = socketFd;
    Fw::Logger::log("Connected to %s:%hu from %s:%hu as a tcp client\n", this->m_ipv4_address, this->m_port,
                    this->m_local_address, this->m_local_port);
    return SOCK_SUCCESS;
}

}  // namespace Drv
