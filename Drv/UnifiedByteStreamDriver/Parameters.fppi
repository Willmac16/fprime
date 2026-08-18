@ Transport used by this driver. NONE leaves the driver disabled.
param TRANSPORT: Drv.ByteStreamTransport \
    default Drv.ByteStreamTransport.NONE

@ Local endpoint to bind, used by TCP_SERVER and UDP. One socket is bound, so an address of
@ 0.0.0.0 takes the given port on every interface, and a port of zero takes one ephemeral
@ port for that bind. The all-zero default is therefore every interface on one ephemeral
@ port, readable afterwards as the LocalPort channel.
param LOCAL_ENDPOINT: Drv.IpEndpoint \
    default { address = 0, $port = 0 }

@ Remote endpoint to talk to. Required by TCP_CLIENT. For UDP the all-zero default means no
@ destination, which sends replies to whoever sent the last datagram. Unused by TCP_SERVER.
param REMOTE_ENDPOINT: Drv.IpEndpoint \
    default { address = 0, $port = 0 }

@ Serial line settings. The device is required when TRANSPORT is SERIAL.
param SERIAL_CONFIG: Drv.SerialConfig

@ Size of the buffers allocated to hold received data. Must be non-zero.
param RECV_BUFFER_SIZE: FwSizeType \
    default 1024

@ Transmit timeout. Used by the IP transports.
param SEND_TIMEOUT: Drv.SendTimeout \
    default { seconds = 1, microseconds = 0 }
