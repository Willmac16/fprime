module Drv {

    @ Size of the serial device path parameter string
    constant BYTE_STREAM_DEVICE_STRING_SIZE = 40

    @ Size of the endpoint description reported in events, e.g. "connect 127.0.0.1:50000"
    constant BYTE_STREAM_ENDPOINT_STRING_SIZE = 60

    @ An IPv4 endpoint: the four address octets plus a port.
    @
    @ Zero is the wildcard in each field: an address of 0.0.0.0 binds every interface, a
    @ local port of zero takes an ephemeral port, and a remote port of zero puts UDP in
    @ reply-to-last-sender mode. An endpoint that is entirely zero is wildcard throughout
    @ and counts as unset, which is what makes the endpoints optional.
    struct IpEndpoint {
        @ Address octets in dotted-quad order, so 127.0.0.1 is [127, 0, 0, 1]
        address: [4] U8
        @ Port in host order
        $port: U16
    } default { address = 0, $port = 0 }

    @ Transport selected by the UnifiedByteStreamDriver TRANSPORT parameter
    enum ByteStreamTransport {
        @ No transport selected: the driver stays disabled
        NONE = 0
        @ TCP: a client when only a remote endpoint is supplied, a server when only a local one is
        TCP = 1
        @ UDP: receives on the local endpoint and/or sends to the remote endpoint
        UDP = 2
        @ Serial: a POSIX (termios) serial device
        SERIAL = 3
    }

    @ Reason a parameter set was rejected as an unsupported configuration
    enum ByteStreamConfigError {
        @ TCP was requested with both a local and a remote endpoint, which no transport serves
        TCP_LOCAL_AND_REMOTE = 0
        @ No endpoint was supplied that the IP transport could use
        NO_ENDPOINT = 1
        @ TCP was given a remote endpoint with a wildcard port, which it cannot connect to
        MISSING_REMOTE_PORT = 2
        @ SERIAL was requested without a device path
        NO_SERIAL_DEVICE = 3
        @ The receive buffer size is zero
        INVALID_BUFFER_SIZE = 4
        @ The send timeout microseconds component is 1000000 or greater
        INVALID_SEND_TIMEOUT = 5
        @ The requested baud rate is not supported by this platform
        UNSUPPORTED_BAUD_RATE = 6
    }

    @ Group of parameters that does not apply to the selected transport
    enum ByteStreamConfigGroup {
        @ LOCAL_ENDPOINT and REMOTE_ENDPOINT
        IP = 0
        @ SERIAL_DEVICE, SERIAL_BAUD_RATE, SERIAL_PARITY and SERIAL_FLOW_CONTROL
        SERIAL = 1
    }

    @ Serial line baud rate. Rates above 230400 are not available on every platform and are
    @ rejected at configuration time when the platform does not define them.
    enum SerialBaudRate: U32 {
        BAUD_9600 = 9600
        BAUD_19200 = 19200
        BAUD_38400 = 38400
        BAUD_57600 = 57600
        BAUD_115200 = 115200
        BAUD_230400 = 230400
        BAUD_460800 = 460800
        BAUD_921600 = 921600
    }

    @ Serial line parity
    enum SerialParity {
        PARITY_NONE = 0
        PARITY_ODD = 1
        PARITY_EVEN = 2
    }

    @ Serial line flow control
    enum SerialFlowControl {
        @ No flow control
        FLOW_NONE = 0
        @ RTS/CTS hardware flow control
        FLOW_HARDWARE = 1
    }

}
