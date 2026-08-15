@ The parameters resolved to a usable configuration
event ConfigurationApplied(
                              transport: Drv.ByteStreamTransport @< The selected transport
                              endpoint: string size Drv.BYTE_STREAM_ENDPOINT_STRING_SIZE @< The resolved endpoint
                          ) \
    severity activity high \
    id 0 \
    format "Byte stream driver configured for {} on {}"

@ TRANSPORT is NONE so the driver will neither send nor receive
event TransportNotConfigured() \
    severity warning high \
    id 1 \
    format "Byte stream driver has no transport selected and stays disabled"

@ The parameters do not describe a configuration the driver can serve. The driver stays
@ disabled rather than coming up half-configured.
event UnsupportedConfiguration(
                                  transport: Drv.ByteStreamTransport @< The requested transport
                                  error: Drv.ByteStreamConfigError @< Why the configuration was rejected
                              ) \
    severity warning high \
    id 2 \
    format "Unsupported {} configuration: {}. Byte stream driver stays disabled"

@ A parameter changed, so the transport was torn down and rebuilt on the new values
event ConfigurationReloaded() \
    severity activity high \
    id 3 \
    format "Byte stream driver parameter changed: transport rebuilt"

@ The transport opened and the driver is ready to carry data
event PortOpened(
                    transport: Drv.ByteStreamTransport @< The selected transport
                    endpoint: string size Drv.BYTE_STREAM_ENDPOINT_STRING_SIZE @< The resolved endpoint
                ) \
    severity activity high \
    id 4 \
    format "Byte stream driver opened {} on {}"

@ The driver could not get a buffer to receive into
event NoBuffers() \
    severity warning high \
    id 5 \
    format "Byte stream driver has no buffer to receive into" \
    throttle 20

@ A transmission failed
event SendError(
                   error: I32 @< The Drv.SocketIpStatus value returned by the transport
               ) \
    severity warning low \
    id 6 \
    format "Byte stream driver failed to send with status {}" \
    throttle 5

@ A reception failed
event ReceiveError(
                      error: I32 @< The Drv.SocketIpStatus value returned by the transport
                  ) \
    severity warning low \
    id 7 \
    format "Byte stream driver failed to receive with status {}" \
    throttle 5
