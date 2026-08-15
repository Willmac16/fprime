// ======================================================================
// \title  TcpClientSocket.hpp
// \author mstarch
// \brief  cpp file for TcpClientSocket core implementation classes
//
// \copyright
// Copyright 2009-2020, by the California Institute of Technology.
// ALL RIGHTS RESERVED.  United States Government Sponsorship
// acknowledged.
//
// ======================================================================
#ifndef DRV_TCPCLIENT_TCPHELPER_HPP_
#define DRV_TCPCLIENT_TCPHELPER_HPP_

#include <Drv/Ip/IpSocket.hpp>
#include <Fw/FPrimeBasicTypes.hpp>
#include <config/IpCfg.hpp>

namespace Drv {
/**
 * \brief Helper for setting up Tcp using Berkeley sockets as a client
 *
 * Certain IP headers have conflicting definitions with the m_data member of various types in fprime. TcpClientSocket
 * separates the ip setup from the incoming Fw::Buffer in the primary component class preventing this collision.
 */
class TcpClientSocket : public IpSocket {
  public:
    /**
     * \brief Constructor for client socket tcp implementation
     */
    TcpClientSocket();

  protected:
    /**
     * \brief Check if the given port is valid for the socket
     *
     * Some ports should be allowed for sockets and disabled on others (e.g. port 0 is a valid tcp server port but not a
     * client. This will check the port and return "true" if the port is valid, or "false" otherwise. In the tcp client
     * implementation, all ports are considered valid except for "0".
     *
     * \param port: port to check
     * \return true if valid, false otherwise
     */
    bool isValidPort(U16 port) const override;

  public:
    /**
     * \brief bind a local endpoint before connecting
     *
     * A TCP client normally takes whatever local endpoint the system gives it. Binding
     * first is how a caller picks the interface a connection leaves by, or a source port a
     * firewall expects. Zero is the wildcard in each field, as it is for any bind, and an
     * entirely zero local endpoint means no bind is done.
     *
     * \param ipv4_address: local IPv4 address (dotted-quad) to bind, "0.0.0.0" for any
     * \param port: local port to bind, 0 for an ephemeral one
     * \return SOCK_SUCCESS, or SOCK_INVALID_CALL when the address does not fit
     */
    SocketIpStatus configureLocal(const char* const ipv4_address, const U16 port);

  protected:
    /**
     * \brief Tcp specific implementation for opening a client socket.
     * \param socketDescriptor: (output) descriptor opened. Only valid on SOCK_SUCCESS. Otherwise will be invalid
     * \return status of open
     */
    SocketIpStatus openProtocol(SocketDescriptor& socketDescriptor) override;
    /**
     * \brief Protocol specific implementation of send.  Called directly with retry from send.
     * \param socketDescriptor: descriptor to send to
     * \param data: data to send
     * \param size: size of data to send
     * \return: size of data sent, or -1 on error.
     */
    FwSignedSizeType sendProtocol(const SocketDescriptor& socketDescriptor,
                                  const U8* const data,
                                  const FwSizeType size) override;
    /**
     * \brief Protocol specific implementation of recv.  Called directly with error handling from recv.
     * \param socketDescriptor: descriptor to recv from
     * \param data: data pointer to fill
     * \param size: size of data buffer
     * \return: size of data received, or -1 on error.
     */
    FwSignedSizeType recvProtocol(const SocketDescriptor& socketDescriptor,
                                  U8* const data,
                                  const FwSizeType size) override;

  private:
    //! \brief whether a local endpoint was asked for at all
    bool hasLocalEndpoint() const;

    char m_local_address[SOCKET_MAX_IPV4_ADDRESS_SIZE] = {};  //!< local IPv4 address to bind, empty for none
    U16 m_local_port = 0;                                     //!< local port to bind, 0 for ephemeral
};
}  // namespace Drv

#endif /* DRV_TCPCLIENT_TCPHELPER_HPP_ */
