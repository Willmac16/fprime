module Drv {

    @ A byte stream driver whose transport is selected entirely by parameters.
    @
    @ Replaces a deployment-time choice between Drv.TcpClient, Drv.TcpServer, Drv.Udp and
    @ Drv.LinuxUartDriver. TRANSPORT names the transport outright; LOCAL_ENDPOINT is what
    @ TCP_SERVER and UDP bind, REMOTE_ENDPOINT is where TCP_CLIENT connects and where UDP
    @ sends, and SERIAL_CONFIG carries the line settings.
    @
    @ The parameters are external, so the same values can be set from a topology in C++ or
    @ by command from the ground without keeping two copies of them.
    passive component UnifiedByteStreamDriver {

        # ----------------------------------------------------------------------
        # General ports
        # ----------------------------------------------------------------------

        @ The synchronous byte stream driver interface this component implements
        import ByteStreamDriver

        @ Buffers for the receive task
        import Svc.BufferAllocation

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
