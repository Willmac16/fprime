module Drv {

    @ Size of the address parameter strings. Large enough for a dotted-quad IPv4 address.
    constant BYTE_STREAM_ADDRESS_STRING_SIZE = 16

    @ Size of the serial device path parameter string
    constant BYTE_STREAM_DEVICE_STRING_SIZE = 40

    @ Size of the endpoint description reported in events, e.g. "127.0.0.1:50000"
    constant BYTE_STREAM_ENDPOINT_STRING_SIZE = 60

    @ Transport requested from the UnifiedByteStreamDriver via the TRANSPORT parameter
    enum ByteStreamTransport {
        @ No transport requested: the driver stays disabled
        NONE = 0
        @ TCP: a client when only a remote endpoint is supplied, a server when only a local endpoint is supplied
        TCP = 1
        @ UDP: receives on the local endpoint and/or sends to the remote endpoint
        UDP = 2
        @ Serial: a POSIX (termios) serial device
        SERIAL = 3
    }

    @ Mode the UnifiedByteStreamDriver resolved from its parameters
    enum ByteStreamDriverMode {
        @ No usable configuration: the driver neither sends nor receives
        DISABLED = 0
        @ TCP client connecting out to the remote endpoint
        TCP_CLIENT = 1
        @ TCP server listening on the local endpoint
        TCP_SERVER = 2
        @ UDP bound to the local endpoint and/or transmitting to the remote endpoint
        UDP = 3
        @ Serial device
        SERIAL = 4
    }

    @ Reason a parameter set was rejected as an unsupported configuration
    enum ByteStreamConfigError {
        @ TCP was requested with both a local and a remote endpoint, which this driver cannot serve
        TCP_LOCAL_AND_REMOTE = 0
        @ Neither a local nor a remote endpoint was supplied for an IP transport
        NO_ENDPOINT = 1
        @ A remote endpoint was supplied without a usable remote port
        MISSING_REMOTE_PORT = 2
        @ A local endpoint was supplied without a usable local port
        MISSING_LOCAL_PORT = 3
        @ SERIAL was requested without a device path
        NO_SERIAL_DEVICE = 4
        @ The local address is not a dotted-quad IPv4 address
        INVALID_LOCAL_ADDRESS = 5
        @ The remote address is not a dotted-quad IPv4 address
        INVALID_REMOTE_ADDRESS = 6
        @ The receive buffer size is zero or larger than this build supports
        INVALID_BUFFER_SIZE = 7
        @ The send timeout microseconds component is 1000000 or greater
        INVALID_SEND_TIMEOUT = 8
        @ The requested baud rate is not supported by this platform
        UNSUPPORTED_BAUD_RATE = 9
    }

    @ Group of parameters that does not apply to the selected transport
    enum ByteStreamConfigGroup {
        @ LOCAL_ADDRESS, LOCAL_PORT, REMOTE_ADDRESS and REMOTE_PORT
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
