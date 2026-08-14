module Drv {

    @ A byte stream driver whose transport is selected entirely by parameters.
    @
    @ Replaces a deployment-time choice between Drv.TcpClient, Drv.TcpServer, Drv.Udp and
    @ Drv.LinuxUartDriver. TRANSPORT picks the transport; an optional local endpoint, an
    @ optional remote endpoint and an optional serial device pick the direction. An
    @ endpoint is unset when its port is zero.
    @
    @ | TRANSPORT | local | remote | behavior                                    |
    @ |-----------|-------|--------|---------------------------------------------|
    @ | TCP       | set   | unset  | listens on the local endpoint               |
    @ | TCP       | unset | set    | connects to the remote endpoint             |
    @ | UDP       | set   | unset  | bound locally, replies to the last sender   |
    @ | UDP       | unset | set    | send-only to the remote endpoint            |
    @ | UDP       | set   | set    | bound locally, sending to the remote        |
    @ | SERIAL    | -     | -      | serial device, IP parameters ignored        |
    @
    @ Anything else is rejected with a warning, leaving the driver unconfigured.
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
