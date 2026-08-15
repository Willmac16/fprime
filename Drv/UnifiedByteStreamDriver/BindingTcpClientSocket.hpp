// ======================================================================
// \title  BindingTcpClientSocket.hpp
// \brief  hpp file for a TCP client socket that can bind a local endpoint
// ======================================================================
#ifndef DRV_UNIFIEDBYTESTREAMDRIVER_BINDINGTCPCLIENTSOCKET_HPP
#define DRV_UNIFIEDBYTESTREAMDRIVER_BINDINGTCPCLIENTSOCKET_HPP

#include <Drv/Ip/TcpClientSocket.hpp>
#include <Fw/FPrimeBasicTypes.hpp>

namespace Drv {

/**
 * \brief a Drv::TcpClientSocket that binds a local endpoint before it connects
 *
 * Drv::TcpClientSocket takes whatever local endpoint the system hands it. Binding first is
 * ordinary TCP - it is how you pick the interface a connection leaves by, or a source port
 * a firewall expects - so this adds the bind and leaves the rest of the client alone.
 *
 * With no local endpoint configured this behaves exactly like its base class.
 */
class BindingTcpClientSocket final : public TcpClientSocket {
  public:
    BindingTcpClientSocket();
    ~BindingTcpClientSocket() override;

    //! \brief bind to this address and port before connecting
    //!
    //! Zero is the wildcard in each field, as it is for any bind: 0.0.0.0 leaves the
    //! interface to the routing table and a zero port takes an ephemeral one. An entirely
    //! zero local endpoint is what the system would have chosen anyway, so no bind is done.
    //!
    //! \return SOCK_INVALID_CALL when the address does not fit
    SocketIpStatus configureLocal(const char* const ipv4_address, const U16 port);

  protected:
    //! \brief open a client socket, binding the configured local endpoint first
    SocketIpStatus openProtocol(SocketDescriptor& socketDescriptor) override;

  private:
    //! \brief whether a local endpoint was asked for at all
    bool hasLocal() const;

    char m_local_address[SOCKET_MAX_IPV4_ADDRESS_SIZE] = {};
    U16 m_local_port = 0;
};

}  // namespace Drv

#endif  // DRV_UNIFIEDBYTESTREAMDRIVER_BINDINGTCPCLIENTSOCKET_HPP
