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

@ Parameters were supplied that do not apply to the selected transport
event IgnoredConfiguration(
                              ignored: Drv.ByteStreamConfigGroup @< The group of ignored parameters
                              transport: Drv.ByteStreamTransport @< The selected transport
                          ) \
    severity warning low \
    id 3 \
    format "Ignoring {} parameters: they do not apply to transport {}"

@ A parameter changed after the driver was configured. The driver applies its
@ configuration once, so the new value takes effect on the next restart.
event ConfigurationChangeDeferred() \
    severity warning low \
    id 4 \
    format "Byte stream driver parameter changed: restart required for it to take effect" \
    throttle 5

@ The transport opened and the driver is ready to carry data
event PortOpened(
                    transport: Drv.ByteStreamTransport @< The selected transport
                    endpoint: string size Drv.BYTE_STREAM_ENDPOINT_STRING_SIZE @< The resolved endpoint
                ) \
    severity activity high \
    id 5 \
    format "Byte stream driver opened {} on {}"

@ The driver could not allocate a buffer to receive into
event NoBuffers() \
    severity warning high \
    id 6 \
    format "Byte stream driver ran out of receive buffers" \
    throttle 20

@ A transmission failed
event SendError(
                   error: I32 @< The Drv.SocketIpStatus value returned by the transport
               ) \
    severity warning low \
    id 7 \
    format "Byte stream driver failed to send with status {}" \
    throttle 5

@ A reception failed
event ReceiveError(
                      error: I32 @< The Drv.SocketIpStatus value returned by the transport
                  ) \
    severity warning low \
    id 8 \
    format "Byte stream driver failed to receive with status {}" \
    throttle 5
