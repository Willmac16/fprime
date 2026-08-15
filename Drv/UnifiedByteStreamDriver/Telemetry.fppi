@ Bytes Sent
telemetry BytesSent: FwSizeType id 0

@ Bytes Received
telemetry BytesRecv: FwSizeType id 1

@ Transport resolved from the parameters
telemetry Transport: Drv.ByteStreamTransport id 2

@ Whether the transport is currently open
telemetry Connected: bool id 3

@ Port the transport is bound to, which for a zero local port is the ephemeral one the
@ system assigned. Zero when the transport binds no port of its own.
telemetry LocalPort: U16 id 4

# The configuration in force, so the resolved values can be read back from the ground
# without a parameter dump. Written whenever the configuration resolves.

@ LOCAL_ENDPOINT in force
telemetry LocalEndpoint: Drv.IpEndpoint id 5

@ REMOTE_ENDPOINT in force
telemetry RemoteEndpoint: Drv.IpEndpoint id 6

@ SERIAL_DEVICE in force
telemetry SerialDevice: string size Drv.BYTE_STREAM_DEVICE_STRING_SIZE id 7

@ SERIAL_BAUD_RATE in force
telemetry SerialBaudRate: Drv.SerialBaudRate id 8

@ SERIAL_PARITY in force
telemetry SerialParity: Drv.SerialParity id 9

@ SERIAL_FLOW_CONTROL in force
telemetry SerialFlowControl: Drv.SerialFlowControl id 10

@ SERIAL_READ_TIMEOUT in force
telemetry SerialReadTimeout: U8 id 11

@ RECV_BUFFER_SIZE in force
telemetry RecvBufferSize: FwSizeType id 12

@ SEND_TIMEOUT in force
telemetry SendTimeout: Drv.SendTimeout id 13
