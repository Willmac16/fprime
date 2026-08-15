// ======================================================================
// \title  TcpClientSocket.cpp
// \author mstarch
// \brief  cpp file for TcpClientSocket core implementation classes
//
// \copyright
// Copyright 2009-2020, by the California Institute of Technology.
// ALL RIGHTS RESERVED.  United States Government Sponsorship
// acknowledged.
//
// ======================================================================

#include <Drv/Ip/TcpClientSocket.hpp>
#include <Fw/FPrimeBasicTypes.hpp>
#include <Fw/Logger/Logger.hpp>
#include <Fw/Types/Assert.hpp>
#include <Fw/Types/StringUtils.hpp>
#include <cstring>

#ifdef TGT_OS_TYPE_VXWORKS
#include <errnoLib.h>
#include <fioLib.h>
#include <hostLib.h>
#include <inetLib.h>
#include <ioLib.h>
#include <sockLib.h>
#include <socket.h>
#include <sysLib.h>
#include <taskLib.h>
#include <vxWorks.h>
#include <cstring>
#elif defined TGT_OS_TYPE_LINUX || TGT_OS_TYPE_DARWIN
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#else
#error OS not supported for IP Socket Communications
#endif

#include <cstdio>
#include <cstring>

namespace Drv {

TcpClientSocket::TcpClientSocket() : IpSocket() {}

bool TcpClientSocket::isValidPort(U16 port) const {
    return port != 0;
}

SocketIpStatus TcpClientSocket::configureLocal(const char* const ipv4_address, const U16 port) {
    FW_ASSERT(ipv4_address != nullptr);
    if (Fw::StringUtils::string_length(ipv4_address, static_cast<FwSizeType>(SOCKET_MAX_IPV4_ADDRESS_SIZE)) >=
        static_cast<FwSizeType>(SOCKET_MAX_IPV4_ADDRESS_SIZE)) {
        return SOCK_INVALID_CALL;
    }
    (void)Fw::StringUtils::string_copy(this->m_local_address, ipv4_address, SOCKET_MAX_IPV4_ADDRESS_SIZE);
    this->m_local_port = port;
    return SOCK_SUCCESS;
}

bool TcpClientSocket::hasLocalEndpoint() const {
    const bool addressed = (this->m_local_address[0] != '\0') && (::strcmp(this->m_local_address, "0.0.0.0") != 0);
    return addressed || (this->m_local_port != 0);
}

SocketIpStatus TcpClientSocket::openProtocol(SocketDescriptor& socketDescriptor) {
    struct sockaddr_in address;

    // Acquire a socket, or return error
    int socketFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socketFd == -1) {
        return SOCK_FAILED_TO_GET_SOCKET;
    }
    // Set up the address port and name
    address.sin_family = AF_INET;
    address.sin_port = htons(this->m_port);

    // OS specific settings
#if defined TGT_OS_TYPE_VXWORKS || TGT_OS_TYPE_DARWIN
    address.sin_len = static_cast<U8>(sizeof(struct sockaddr_in));
#endif

    // Convert the configured IPv4 address (dotted-quad) to a network-order in_addr.

    const SocketIpStatus addressStatus = IpSocket::addressToIp4(this->m_ipv4_address, &(address.sin_addr));
    if (addressStatus != SOCK_SUCCESS) {
        (void)::close(socketFd);
        return SOCK_INVALID_IP_ADDRESS;
    };

    if (IpSocket::setupSocketOptions(socketFd) != SOCK_SUCCESS) {
        (void)::close(socketFd);
        return SOCK_FAILED_TO_SET_SOCKET_OPTIONS;
    }

    // Now apply timeouts
    if (IpSocket::setupTimeouts(socketFd) != SOCK_SUCCESS) {
        (void)::close(socketFd);
        return SOCK_FAILED_TO_SET_SOCKET_OPTIONS;
    }

    // Bind the local endpoint first when one was asked for, so the connection leaves by the
    // interface and source port the caller chose
    if (this->hasLocalEndpoint()) {
        struct sockaddr_in local;
        (void)::memset(&local, 0, sizeof(local));
        local.sin_family = AF_INET;
        local.sin_port = htons(this->m_local_port);
#if defined TGT_OS_TYPE_VXWORKS || TGT_OS_TYPE_DARWIN
        local.sin_len = static_cast<U8>(sizeof(struct sockaddr_in));
#endif
        if (IpSocket::addressToIp4(this->m_local_address, &local.sin_addr) != SOCK_SUCCESS) {
            (void)::close(socketFd);
            return SOCK_INVALID_IP_ADDRESS;
        }
        if (::bind(socketFd, reinterpret_cast<struct sockaddr*>(&local), sizeof(local)) < 0) {
            (void)::close(socketFd);
            return SOCK_FAILED_TO_BIND;
        }
    }

    // TCP requires connect to the socket to allow for communication
    if (::connect(socketFd, reinterpret_cast<struct sockaddr*>(&address), sizeof(address)) < 0) {
        (void)::close(socketFd);
        return SOCK_FAILED_TO_CONNECT;
    }
    socketDescriptor.fd = socketFd;
    Fw::Logger::log("Connected to %s:%hu as a tcp client\n", this->m_ipv4_address, this->m_port);
    return SOCK_SUCCESS;
}

FwSignedSizeType TcpClientSocket::sendProtocol(const SocketDescriptor& socketDescriptor,
                                               const U8* const data,
                                               const FwSizeType size) {
    return static_cast<FwSignedSizeType>(
        ::send(socketDescriptor.fd, data, static_cast<size_t>(size), SOCKET_IP_SEND_FLAGS));
}

FwSignedSizeType TcpClientSocket::recvProtocol(const SocketDescriptor& socketDescriptor,
                                               U8* const data,
                                               const FwSizeType size) {
    return static_cast<FwSignedSizeType>(
        ::recv(socketDescriptor.fd, data, static_cast<size_t>(size), SOCKET_IP_RECV_FLAGS));
}

}  // namespace Drv
