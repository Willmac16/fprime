module Drv {

    @ A byte stream driver whose transport is selected entirely by parameters.
    @
    @ Replaces a deployment-time choice between Drv.TcpClient, Drv.TcpServer, Drv.Udp and
    @ Drv.LinuxUartDriver. TRANSPORT picks the transport, REMOTE_ENDPOINT picks the
    @ direction, and SERIAL_DEVICE names the line.
    @
    @ Zero means what it already means to the transports. LOCAL_ENDPOINT is bound, so its
    @ zeros are wildcards: an entirely zero one binds every interface on an ephemeral port.
    @ REMOTE_ENDPOINT is a destination, so an entirely zero one is no destination at all.
    @
    @ | TRANSPORT | remote    | behavior                                          |
    @ |-----------|-----------|---------------------------------------------------|
    @ | TCP       | reachable | connects to it                                    |
    @ | TCP       | none      | listens on the local endpoint                     |
    @ | UDP       | reachable | binds the local endpoint, sends to the remote      |
    @ | UDP       | none      | binds the local endpoint, replies to last sender   |
    @ | SERIAL    | -         | serial device, IP parameters ignored               |
    @
    @ A remote endpoint that is only partly zero, and a local endpoint asked for alongside
    @ a TCP remote, are rejected with a warning that leaves the driver unconfigured.
    passive component UnifiedByteStreamDriver {

        # ----------------------------------------------------------------------
        # General ports
        # ----------------------------------------------------------------------

        @ The synchronous byte stream driver interface this component implements
        import ByteStreamDriver

        @ Allocation port used for allocating memory in the receive task
        output port allocate: Fw.BufferGet

        @ Deallocation of allocated buffers
        output port deallocate: Fw.BufferSend

        @ The rate group input for sending telemetry
        sync input port run: Svc.Sched

        # ----------------------------------------------------------------------
        # Parameters, events, telemetry
        # ----------------------------------------------------------------------

        include "Parameters.fppi"

        include "Events.fppi"

        include "Telemetry.fppi"

        # ----------------------------------------------------------------------
        # Special ports
        # ----------------------------------------------------------------------

        @ Enables command handling, required by the parameter set and save commands
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
