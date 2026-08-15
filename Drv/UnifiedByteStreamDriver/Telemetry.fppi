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

# Each parameter in force, under the parameter's own name, so the configuration can be read
# back from the ground without a parameter dump.

@ TRANSPORT in force
telemetry TRANSPORT: Drv.ByteStreamTransport

@ LOCAL_ENDPOINT in force
telemetry LOCAL_ENDPOINT: Drv.IpEndpoint

@ REMOTE_ENDPOINT in force
telemetry REMOTE_ENDPOINT: Drv.IpEndpoint

@ SERIAL_CONFIG in force
telemetry SERIAL_CONFIG: Drv.SerialConfig

@ RECV_BUFFER_SIZE in force
telemetry RECV_BUFFER_SIZE: FwSizeType

@ SEND_TIMEOUT in force
telemetry SEND_TIMEOUT: Drv.SendTimeout
