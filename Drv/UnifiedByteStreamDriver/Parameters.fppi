@ Every parameter here is external: the component owns the storage, so a topology can set
@ it directly in C++ and the ground can set it by command, both landing in the same place.

@ Transport used by this driver. NONE leaves the driver disabled.
external param TRANSPORT: Drv.ByteStreamTransport \
    default Drv.ByteStreamTransport.NONE \
    id 0

@ Local endpoint to bind, used by every IP transport including TCP_CLIENT. An address of
@ 0.0.0.0 binds every interface and a port of zero takes an ephemeral port, so the all-zero
@ default binds every interface on an ephemeral port.
external param LOCAL_ENDPOINT: Drv.IpEndpoint \
    default { address = 0, $port = 0 } \
    id 1

@ Remote endpoint to talk to. Required by TCP_CLIENT. For UDP the all-zero default means no
@ destination, which sends replies to whoever sent the last datagram. Unused by TCP_SERVER.
external param REMOTE_ENDPOINT: Drv.IpEndpoint \
    default { address = 0, $port = 0 } \
    id 2

@ Serial device path, e.g. "/dev/ttyUSB0". Required when TRANSPORT is SERIAL.
external param SERIAL_DEVICE: string size Drv.BYTE_STREAM_DEVICE_STRING_SIZE \
    default "" \
    id 3

@ Serial line baud rate
external param SERIAL_BAUD_RATE: Drv.SerialBaudRate \
    default Drv.SerialBaudRate.BAUD_115200 \
    id 4

@ Serial line parity
external param SERIAL_PARITY: Drv.SerialParity \
    default Drv.SerialParity.PARITY_NONE \
    id 5

@ Serial line flow control
external param SERIAL_FLOW_CONTROL: Drv.SerialFlowControl \
    default Drv.SerialFlowControl.FLOW_NONE \
    id 6

@ How long a serial read waits on an idle line before returning empty, in tenths of a
@ second (termios VTIME). This is what bounds how long stopping a serial link takes.
external param SERIAL_READ_TIMEOUT: U8 \
    default 10 \
    id 7

@ Size of the buffers allocated to hold received data. Must be non-zero.
external param RECV_BUFFER_SIZE: FwSizeType \
    default 1024 \
    id 8

@ Transmit timeout. Used by the IP transports.
external param SEND_TIMEOUT: Drv.SendTimeout \
    default { seconds = 1, microseconds = 0 } \
    id 9
