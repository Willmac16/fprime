module Drv {

    @ Size of the serial device path parameter string
    constant BYTE_STREAM_DEVICE_STRING_SIZE = 40

    @ Size of the endpoint description reported in events, e.g. "connect 127.0.0.1:50000"
    constant BYTE_STREAM_ENDPOINT_STRING_SIZE = 60

    @ An IPv4 endpoint: the four address octets plus a port.
    @
    @ Zero means what it already means to the transports. On a local endpoint, which is
    @ bound, an address of 0.0.0.0 binds every interface and a port of zero takes an
    @ ephemeral one. On a remote endpoint, which is a destination, zero addresses nothing,
    @ so an entirely zero remote endpoint is no destination at all.
    struct IpEndpoint {
        @ Address octets in dotted-quad order, so 127.0.0.1 is [127, 0, 0, 1]
        address: [4] U8
        @ Port in host order
        $port: U16
    } default { address = 0, $port = 0 }

    @ Time allowed for a transmission before it gives up.
    @
    @ A microseconds field of 1000000 or more is carried into seconds when the timeout is
    @ used, so 0s 1500000us is the same timeout as 1s 500000us.
    struct SendTimeout {
        seconds: U32
        microseconds: U32
    } default { seconds = 1, microseconds = 0 }

    @ Transport used by the UnifiedByteStreamDriver
    enum ByteStreamTransport {
        @ No transport selected: the driver stays disabled
        NONE = 0
        @ TCP connecting out to the remote endpoint
        TCP_CLIENT = 1
        @ TCP listening on the local endpoint
        TCP_SERVER = 2
        @ UDP, bound to the local endpoint
        UDP = 3
        @ A POSIX (termios) serial device
        SERIAL = 4
    }

    @ Reason a parameter set was rejected as an unsupported configuration
    enum ByteStreamConfigError {
        @ TCP_CLIENT was requested with no remote endpoint to connect to
        MISSING_REMOTE_ENDPOINT = 0
        @ The remote endpoint is only partly zero, so it is neither a destination that can
        @ be reached nor the entirely zero endpoint that means no destination at all
        INCOMPLETE_REMOTE_ENDPOINT = 1
        @ SERIAL was requested without a device path
        NO_SERIAL_DEVICE = 2
        @ The receive buffer size is zero
        INVALID_BUFFER_SIZE = 3
        @ The requested baud rate is not supported by this platform
        UNSUPPORTED_BAUD_RATE = 4
        @ The transport rejected the settings it was given
        TRANSPORT_REJECTED_SETTINGS = 5
    }

    @ Drv::SocketIpStatus, which has no FPP declaration of its own, mirrored so the ground
    @ sees a name rather than a number. UnifiedByteStreamDriver.cpp static_asserts the two
    @ against each other, so a change to either fails the build.
    enum SocketStatus: I32 {
        SUCCESS = 0
        FAILED_TO_GET_SOCKET = -1
        FAILED_TO_GET_HOST_IP = -2
        INVALID_IP_ADDRESS = -3
        FAILED_TO_CONNECT = -4
        FAILED_TO_SET_SOCKET_OPTIONS = -5
        INTERRUPTED_TRY_AGAIN = -6
        READ_ERROR = -7
        DISCONNECTED = -8
        FAILED_TO_BIND = -9
        FAILED_TO_LISTEN = -10
        FAILED_TO_ACCEPT = -11
        SEND_ERROR = -13
        NOT_STARTED = -14
        FAILED_TO_READ_BACK_PORT = -15
        NO_DATA_AVAILABLE = -16
        ANOTHER_THREAD_OPENING = -17
        AUTO_CONNECT_DISABLED = -18
        INVALID_CALL = -19
    }

    @ Serial line baud rate.
    @
    @ Linux termios speeds are small indices rather than rates, so every rate here has to be
    @ mapped to its B constant, and a rate whose constant this platform does not define is
    @ rejected rather than silently run at the wrong speed. BSD and macOS take the rate
    @ itself, so they reach the higher rates without a B constant existing for them.
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
        @ XON/XOFF software flow control
        FLOW_SOFTWARE = 2
    }

    @ Everything needed to open a serial line
    struct SerialConfig {
        @ Device path, e.g. "/dev/ttyUSB0". Required when TRANSPORT is SERIAL.
        device: string size BYTE_STREAM_DEVICE_STRING_SIZE
        baudRate: SerialBaudRate
        parity: SerialParity
        flowControl: SerialFlowControl
        @ How long a read waits on an idle line before returning empty, in tenths of a
        @ second (termios VTIME). This is what bounds how long stopping a serial link takes.
        readTimeout: U8
    } default {
        device = ""
        baudRate = SerialBaudRate.BAUD_115200
        parity = SerialParity.PARITY_NONE
        flowControl = SerialFlowControl.FLOW_NONE
        readTimeout = 10
    }

}
