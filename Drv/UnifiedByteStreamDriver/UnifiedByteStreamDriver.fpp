module Drv {

    @ A byte stream driver whose transport is selected entirely by parameters.
    @
    @ A single instance of this component replaces a deployment-time choice between
    @ Drv.TcpClient, Drv.TcpServer, Drv.Udp and Drv.LinuxUartDriver. The transport is
    @ picked with the TRANSPORT parameter, and the endpoints are supplied as an optional
    @ local address/port pair, an optional remote address/port pair, and an optional
    @ serial device. Combinations that the underlying transports cannot serve are
    @ rejected with a warning event and leave the driver disabled rather than
    @ half-configured.
    @
    @ Supported combinations:
    @ | TRANSPORT | local | remote | resolved mode                             |
    @ |-----------|-------|--------|-------------------------------------------|
    @ | TCP       | set   | unset  | TCP server listening on the local endpoint |
    @ | TCP       | unset | set    | TCP client connecting to the remote        |
    @ | UDP       | set   | unset  | UDP bound locally, replies to last sender  |
    @ | UDP       | unset | set    | UDP send-only to the remote endpoint       |
    @ | UDP       | set   | set    | UDP bound locally, sending to the remote   |
    @ | SERIAL    | -     | -      | serial device, IP parameters ignored       |
    passive component UnifiedByteStreamDriver {

        # ----------------------------------------------------------------------
        # General ports
        # ----------------------------------------------------------------------

        import ByteStreamDriver

        @ Allocation port used for allocating memory in the receive task
        output port allocate: Fw.BufferGet

        @ Deallocation of allocated buffers
        output port deallocate: Fw.BufferSend

        @ The rate group input for sending telemetry
        sync input port run: Svc.Sched

        # ----------------------------------------------------------------------
        # Parameters
        # ----------------------------------------------------------------------

        @ Transport used by this driver. NONE leaves the driver disabled.
        param TRANSPORT: Drv.ByteStreamTransport default Drv.ByteStreamTransport.NONE

        @ Local IPv4 address in dotted-quad form, e.g. "0.0.0.0". Empty leaves the local
        @ endpoint unset. No DNS resolution is performed: host names are rejected.
        param LOCAL_ADDRESS: string size Drv.BYTE_STREAM_ADDRESS_STRING_SIZE default ""

        @ Local port. Ignored when LOCAL_ADDRESS is empty. 0 requests an ephemeral port,
        @ which is supported for TCP servers and for UDP receive.
        param LOCAL_PORT: U16 default 0

        @ Remote IPv4 address in dotted-quad form, e.g. "127.0.0.1". Empty leaves the
        @ remote endpoint unset. No DNS resolution is performed: host names are rejected.
        param REMOTE_ADDRESS: string size Drv.BYTE_STREAM_ADDRESS_STRING_SIZE default ""

        @ Remote port. Ignored when REMOTE_ADDRESS is empty, and must be non-zero otherwise.
        param REMOTE_PORT: U16 default 0

        @ Serial device path, e.g. "/dev/ttyUSB0". Required when TRANSPORT is SERIAL.
        param SERIAL_DEVICE: string size Drv.BYTE_STREAM_DEVICE_STRING_SIZE default ""

        @ Serial line baud rate. Used when TRANSPORT is SERIAL.
        param SERIAL_BAUD_RATE: Drv.SerialBaudRate default Drv.SerialBaudRate.BAUD_115200

        @ Serial line parity. Used when TRANSPORT is SERIAL.
        param SERIAL_PARITY: Drv.SerialParity default Drv.SerialParity.PARITY_NONE

        @ Serial line flow control. Used when TRANSPORT is SERIAL.
        param SERIAL_FLOW_CONTROL: Drv.SerialFlowControl default Drv.SerialFlowControl.FLOW_NONE

        @ Size of the buffers allocated to hold received data. Must be non-zero.
        param RECV_BUFFER_SIZE: FwSizeType default 1024

        @ Seconds component of the transmit timeout. Used by the IP transports.
        param SEND_TIMEOUT_SECONDS: U32 default 1

        @ Microseconds component of the transmit timeout. Must be less than 1000000.
        @ Used by the IP transports.
        param SEND_TIMEOUT_MICROSECONDS: U32 default 0

        # ----------------------------------------------------------------------
        # Events
        # ----------------------------------------------------------------------

        @ The parameters resolved to a usable configuration
        event ConfigurationApplied(
                                      mode: Drv.ByteStreamDriverMode @< The resolved mode
                                      endpoint: string size Drv.BYTE_STREAM_ENDPOINT_STRING_SIZE @< The resolved endpoint
                                  ) \
            severity activity high \
            format "Byte stream driver configured as {} on {}"

        @ TRANSPORT is NONE so the driver will neither send nor receive
        event TransportNotConfigured() \
            severity warning high \
            format "Byte stream driver has no transport selected and stays disabled"

        @ The parameters do not describe a configuration the driver can serve. The driver
        @ stays disabled rather than coming up half-configured.
        event UnsupportedConfiguration(
                                          transport: Drv.ByteStreamTransport @< The requested transport
                                          error: Drv.ByteStreamConfigError @< Why the configuration was rejected
                                      ) \
            severity warning high \
            format "Unsupported {} configuration: {}. Byte stream driver stays disabled"

        @ Parameters were supplied that do not apply to the selected transport
        event IgnoredConfiguration(
                                      ignored: Drv.ByteStreamConfigGroup @< The group of ignored parameters
                                      transport: Drv.ByteStreamTransport @< The selected transport
                                  ) \
            severity warning low \
            format "Ignoring {} parameters: they do not apply to transport {}"

        @ A parameter changed after the driver was configured. The driver applies its
        @ configuration once, so the new value takes effect on the next restart.
        event ConfigurationChangeDeferred() \
            severity warning low \
            format "Byte stream driver parameter changed: restart required for it to take effect" \
            throttle 5

        @ The transport opened and the driver is ready to carry data
        event PortOpened(
                            mode: Drv.ByteStreamDriverMode @< The resolved mode
                            endpoint: string size Drv.BYTE_STREAM_ENDPOINT_STRING_SIZE @< The resolved endpoint
                        ) \
            severity activity high \
            format "Byte stream driver opened {} on {}"

        @ The driver could not allocate a buffer to receive into
        event NoBuffers() \
            severity warning high \
            format "Byte stream driver ran out of receive buffers" \
            throttle 20

        @ A transmission failed
        event SendError(
                           error: I32 @< The Drv.SocketIpStatus value returned by the transport
                       ) \
            severity warning low \
            format "Byte stream driver failed to send with status {}" \
            throttle 5

        @ A reception failed
        event ReceiveError(
                              error: I32 @< The Drv.SocketIpStatus value returned by the transport
                          ) \
            severity warning low \
            format "Byte stream driver failed to receive with status {}" \
            throttle 5

        # ----------------------------------------------------------------------
        # Telemetry
        # ----------------------------------------------------------------------

        @ Bytes sent since startup
        telemetry BytesSent: FwSizeType

        @ Bytes received since startup
        telemetry BytesReceived: FwSizeType

        @ Mode resolved from the parameters
        telemetry Mode: Drv.ByteStreamDriverMode

        @ Whether the transport is currently open
        telemetry Connected: bool

        # ----------------------------------------------------------------------
        # Special ports
        # ----------------------------------------------------------------------

        @ Enables command handling, required by the parameter set/save commands
        import Fw.Command

        @ Enables event handling
        import Fw.Event

        @ Enables telemetry
        import Fw.Channel

        @ Port for requesting the current time
        time get port timeCaller

        @ Port to return the value of a parameter
        param get port prmGetOut

        @ Port to set the value of a parameter
        param set port prmSetOut

    }

}
