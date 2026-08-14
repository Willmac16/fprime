@ Transport used by this driver. NONE leaves the driver disabled.
param TRANSPORT: Drv.ByteStreamTransport \
    default Drv.ByteStreamTransport.NONE \
    id 0

@ Local endpoint to bind. An address of 0.0.0.0 binds every interface and a port of zero
@ takes an ephemeral port, so the all-zero default binds every interface on an ephemeral
@ port. Unused by a TCP link that connects out, which binds where the system tells it to.
param LOCAL_ENDPOINT: Drv.IpEndpoint \
    default { address = 0, $port = 0 } \
    id 1

@ Remote endpoint to talk to, and what picks the direction of an IP link. The all-zero
@ default is no destination: TCP listens on the local endpoint and UDP replies to whoever
@ sent it the last datagram. A partly zero endpoint is neither and is rejected.
param REMOTE_ENDPOINT: Drv.IpEndpoint \
    default { address = 0, $port = 0 } \
    id 2

@ Serial device path, e.g. "/dev/ttyUSB0". Required when TRANSPORT is SERIAL.
param SERIAL_DEVICE: string size Drv.BYTE_STREAM_DEVICE_STRING_SIZE \
    default "" \
    id 3

@ Serial line baud rate. Used when TRANSPORT is SERIAL.
param SERIAL_BAUD_RATE: Drv.SerialBaudRate \
    default Drv.SerialBaudRate.BAUD_115200 \
    id 4

@ Serial line parity. Used when TRANSPORT is SERIAL.
param SERIAL_PARITY: Drv.SerialParity \
    default Drv.SerialParity.PARITY_NONE \
    id 5

@ Serial line flow control. Used when TRANSPORT is SERIAL.
param SERIAL_FLOW_CONTROL: Drv.SerialFlowControl \
    default Drv.SerialFlowControl.FLOW_NONE \
    id 6

@ Size of the buffers allocated to hold received data. Must be non-zero.
param RECV_BUFFER_SIZE: FwSizeType \
    default 1024 \
    id 7

@ Seconds component of the transmit timeout. Used by the IP transports.
param SEND_TIMEOUT_SECONDS: U32 \
    default 1 \
    id 8

@ Microseconds component of the transmit timeout. Must be less than 1000000.
@ Used by the IP transports.
param SEND_TIMEOUT_MICROSECONDS: U32 \
    default 0 \
    id 9
