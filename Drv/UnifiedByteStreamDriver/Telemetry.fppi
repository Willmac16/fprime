@ Bytes Sent
telemetry BytesSent: FwSizeType

@ Bytes Received
telemetry BytesRecv: FwSizeType

@ Transport the driver is running, which is NONE when the parameters were rejected
telemetry ActiveTransport: Drv.ByteStreamTransport

@ Whether the transport is currently open
telemetry Connected: bool

@ Port the transport is bound to, which for a zero local port is the ephemeral one the
@ system assigned. Zero when the transport binds no port of its own.
telemetry LocalPort: U16

# Each parameter as set, under the parameter's own name, so the configuration can be read
# back from the ground without a parameter dump. These report the setting, not the transport
# built from it, so a change shows up here as soon as it is made. ActiveTransport is what the
# driver is actually running.

@ TRANSPORT as set
telemetry TRANSPORT: Drv.ByteStreamTransport

@ LOCAL_ENDPOINT as set
telemetry LOCAL_ENDPOINT: Drv.IpEndpoint

@ REMOTE_ENDPOINT as set
telemetry REMOTE_ENDPOINT: Drv.IpEndpoint

@ SERIAL_CONFIG as set
telemetry SERIAL_CONFIG: Drv.SerialConfig

@ RECV_BUFFER_SIZE as set
telemetry RECV_BUFFER_SIZE: FwSizeType

@ SEND_TIMEOUT as set
telemetry SEND_TIMEOUT: Drv.SendTimeout
