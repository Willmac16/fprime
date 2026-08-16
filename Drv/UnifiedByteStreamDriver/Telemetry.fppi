@ Bytes Sent
telemetry BytesSent: FwSizeType

@ Bytes Received
telemetry BytesRecv: FwSizeType

@ Transport the driver is running, which is NONE when the parameters were rejected
telemetry ActiveTransport: Drv.ByteStreamTransport

@ Endpoint the running transport is configured for, e.g. "bind 127.0.0.1:50000"
telemetry ActiveEndpoint: string size Drv.BYTE_STREAM_ENDPOINT_STRING_SIZE

@ Whether the transport is currently open
telemetry Connected: bool

@ Port the transport is bound to, which for a zero local port is the ephemeral one the
@ system assigned. Zero when the transport binds no port of its own.
telemetry LocalPort: U16

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
