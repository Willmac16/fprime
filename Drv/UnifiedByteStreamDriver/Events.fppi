@ The parameters resolved to a usable configuration
event ConfigurationApplied(
                              transport: Drv.ByteStreamTransport @< The selected transport
                              endpoint: string size Drv.BYTE_STREAM_ENDPOINT_STRING_SIZE @< The resolved endpoint
                          ) \
    severity activity high \
    format "Byte stream driver configured for {} on {}"

@ The parameters do not describe a configuration the driver can serve. The driver stays
@ disabled rather than coming up half-configured.
event UnsupportedConfiguration(
                                  transport: Drv.ByteStreamTransport @< The requested transport
                                  error: Drv.ByteStreamConfigError @< Why the configuration was rejected
                              ) \
    severity warning high \
    format "Unsupported {} configuration: {}. Byte stream driver stays disabled"

@ The transport opened and the driver is ready to carry data
event PortOpened(
                    transport: Drv.ByteStreamTransport @< The selected transport
                    endpoint: string size Drv.BYTE_STREAM_ENDPOINT_STRING_SIZE @< The resolved endpoint
                ) \
    severity activity high \
    format "Byte stream driver opened {} on {}"

@ Something was sent to a driver with no transport selected, so it went nowhere
event SendWithoutTransport() \
    severity warning high \
    format "Byte stream driver has no transport selected and cannot send" \
    throttle 1 every { seconds = 5, useconds = 0 }

@ The driver could not get a buffer to receive into
event NoBuffers() \
    severity warning high \
    format "Byte stream driver has no buffer to receive into" \
    throttle 1 every { seconds = 5, useconds = 0 }

@ A transmission failed
event SendError(
                   error: Drv.SocketStatus @< The status returned by the transport
               ) \
    severity warning low \
    format "Byte stream driver failed to send: {}" \
    throttle 1 every { seconds = 5, useconds = 0 }

@ A reception failed
event ReceiveError(
                      error: Drv.SocketStatus @< The status returned by the transport
                  ) \
    severity warning low \
    format "Byte stream driver failed to receive: {}" \
    throttle 1 every { seconds = 5, useconds = 0 }
