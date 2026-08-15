@ Every parameter here is external: the component owns the storage, so a topology can set
@ it directly in C++ and the ground can set it by command, both landing in the same place.

@ Transport used by this driver. NONE leaves the driver disabled.
external param TRANSPORT: Drv.ByteStreamTransport \
    default Drv.ByteStreamTransport.NONE

@ Local endpoint to bind, used by TCP_SERVER and UDP. An address of 0.0.0.0 binds every
@ interface and a port of zero takes an ephemeral port, so the all-zero default binds every
@ interface on an ephemeral port.
external param LOCAL_ENDPOINT: Drv.IpEndpoint \
    default { address = 0, $port = 0 }

@ Remote endpoint to talk to. Required by TCP_CLIENT. For UDP the all-zero default means no
@ destination, which sends replies to whoever sent the last datagram. Unused by TCP_SERVER.
external param REMOTE_ENDPOINT: Drv.IpEndpoint \
    default { address = 0, $port = 0 }

@ Serial line settings. The device is required when TRANSPORT is SERIAL.
external param SERIAL_CONFIG: Drv.SerialConfig

@ Size of the buffers allocated to hold received data. Must be non-zero.
external param RECV_BUFFER_SIZE: FwSizeType \
    default 1024

@ Transmit timeout. Used by the IP transports.
external param SEND_TIMEOUT: Drv.SendTimeout \
    default { seconds = 1, microseconds = 0 }
