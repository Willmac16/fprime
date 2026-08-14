# Drv::UnifiedByteStreamDriver Component

`Drv::UnifiedByteStreamDriver` is a byte stream driver whose transport is chosen by
parameters rather than by the topology. One instance covers what `Drv::TcpClient`,
`Drv::TcpServer`, `Drv::Udp` and `Drv::LinuxUartDriver` each cover individually, so moving
a link from, say, a UDP socket to a serial line is a parameter change rather than a
topology change and a rebuild.

For the interface this component implements, see
[`Drv::ByteStreamDriverModel`](../../ByteStreamDriverModel/docs/sdd.md). For the underlying
socket implementations, see [`Drv::Ip`](../../Ip/docs/sdd.md).

## Design

The component implements the synchronous `Drv.ByteStreamDriver` interface: `send` in,
`recv` and `ready` out, and `recvReturnIn` to take buffers back. It holds all four
transports as members and hands whichever one the parameters selected to
`Drv::SocketComponentHelper`, which supplies the read task, the reconnect task, and the
open/close lifecycle shared with the other IP drivers.

The serial transport reaches that machinery through `Drv::SerialStream`, which implements
the `Drv::IpSocket` interface — really a byte-stream endpoint interface of
`openProtocol`/`sendProtocol`/`recvProtocol` plus close and shutdown — on top of a POSIX
termios device. That is what lets one component drive TCP, UDP and serial through exactly
the same read loop. The cost is that `Drv::SocketIpStatus` also carries non-socket errors
for the serial case; the mapping is documented in `SerialStream.hpp`.

Holding all four transports as members costs a few hundred bytes and avoids dynamic
allocation, which matters more.

### Resolving the configuration

`TRANSPORT` selects the transport. The endpoints are supplied as an optional local
address/port pair, an optional remote address/port pair, and an optional serial device. An
address/port pair counts as supplied when its address parameter is non-empty; a port of 0
is meaningful on its own (an ephemeral port) and so cannot be used as the "unset" marker.

| `TRANSPORT` | local | remote | resolved mode                                        |
|-------------|-------|--------|------------------------------------------------------|
| `TCP`       | set   | unset  | `TCP_SERVER`, listening on the local endpoint        |
| `TCP`       | unset | set    | `TCP_CLIENT`, connecting to the remote endpoint      |
| `TCP`       | set   | set    | rejected: no transport here both binds and connects  |
| `TCP`       | unset | unset  | rejected: nothing to listen on or connect to         |
| `UDP`       | set   | unset  | `UDP`, bound locally, replying to the last sender    |
| `UDP`       | unset | set    | `UDP`, send-only to the remote endpoint              |
| `UDP`       | set   | set    | `UDP`, bound locally and sending to the remote       |
| `UDP`       | unset | unset  | rejected: nothing to bind or send to                 |
| `SERIAL`    | any   | any    | `SERIAL`; the IP parameters are warned about, unused |
| `NONE`      | any   | any    | disabled, with a warning                             |

A combination that cannot be served emits `UnsupportedConfiguration` and leaves the driver
disabled. It does not come up half-configured: a link that silently works in only one
direction is worse than one that refuses to start and says why. Parameters that do not
apply to the selected transport emit `IgnoredConfiguration`, which is a warning rather
than a rejection — the link still comes up.

These further checks are applied before any transport is touched:

- Addresses must be dotted-quad IPv4. The IP transports do not resolve host names, so
  `"localhost"` is rejected up front instead of failing later at open time. Leading zeros
  are rejected for the same reason `inet_pton` rejects them.
- A remote endpoint needs a non-zero port; there is nothing to send to on port 0.
- `RECV_BUFFER_SIZE` must be non-zero.
- `SEND_TIMEOUT_MICROSECONDS` must be less than 1000000.
- The requested `SERIAL_BAUD_RATE` must be one this platform's termios defines. Rates above
  230400 are optional and are rejected where they are unavailable.

### When the configuration is resolved

The configuration is resolved exactly once, in `parametersLoaded` — or on the first
`start` for deployments that have no parameter database and therefore never call
`loadParameters`. Changing a parameter afterwards emits `ConfigurationChangeDeferred` and
leaves the running link alone: rebuilding a transport underneath a live read task is not
safe, so the new value takes effect at the next restart.

A rejected configuration is also final. It is not retried behind the operator's back — the
warning it emitted is the whole answer, and `start` on a disabled driver runs no tasks.

## Usage

Connect the component like any other byte stream driver, plus the `allocate`/`deallocate`
ports, a rate group on `run` for telemetry, and the standard command, event, telemetry,
time and parameter ports. Then start it once parameters are available:

```c++
Drv::UnifiedByteStreamDriver comm("comm");

void startTasks() {
    ...
    // loadParameters resolves the configuration; start brings up the transport
    comm.start();
}

void exitTasks() {
    ...
    comm.stop();
    (void) comm.join();
}
```

`start` is a no-op when the parameters were rejected, so no caller needs to check the mode
first. `getMode` reports what the parameters resolved to, and `getLocalPort` reports the
port actually in use, which is how an ephemeral port (a `LOCAL_PORT` of 0) is read back.

### Parameters

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `TRANSPORT` | `Drv.ByteStreamTransport` | `NONE` | Transport to use |
| `LOCAL_ADDRESS` | string | `""` | Local IPv4 address; empty leaves the local endpoint unset |
| `LOCAL_PORT` | `U16` | 0 | Local port; 0 requests an ephemeral port |
| `REMOTE_ADDRESS` | string | `""` | Remote IPv4 address; empty leaves the remote endpoint unset |
| `REMOTE_PORT` | `U16` | 0 | Remote port; must be non-zero when a remote address is set |
| `SERIAL_DEVICE` | string | `""` | Serial device path, e.g. `/dev/ttyUSB0` |
| `SERIAL_BAUD_RATE` | `Drv.SerialBaudRate` | `BAUD_115200` | Baud rate of the line |
| `SERIAL_PARITY` | `Drv.SerialParity` | `PARITY_NONE` | Parity of the line |
| `SERIAL_FLOW_CONTROL` | `Drv.SerialFlowControl` | `FLOW_NONE` | Flow control of the line |
| `RECV_BUFFER_SIZE` | `FwSizeType` | 1024 | Size of the buffers allocated for receiving |
| `SEND_TIMEOUT_SECONDS` | `U32` | 1 | Seconds component of the transmit timeout |
| `SEND_TIMEOUT_MICROSECONDS` | `U32` | 0 | Microseconds component of the transmit timeout |

### Serial specifics

The device is opened in non-canonical mode with 8 data bits and `VMIN=0`/`VTIME=10`, so a
read on an idle line returns after roughly a second. `Drv::SerialStream` absorbs those
empty reads and keeps reading, so an idle line does not push a stream of empty receives at
the rest of the system; `stop` asks the blocked read to return so the read task can exit.
That is also why stopping a serial link can take up to a second.

## Port Descriptions

| Name | Description |
|---|---|
| `send` | Guarded input; sends data out of the driver and returns a status |
| `recv` | Output; delivers received data with a status |
| `recvReturnIn` | Guarded input; returns ownership of a buffer sent out on `recv` |
| `ready` | Output; invoked when the transport has opened |
| `allocate` | Output; allocates the buffers the read task fills |
| `deallocate` | Output; returns buffers handed back on `recvReturnIn` |
| `run` | Sync input; rate group tick that emits telemetry |

## Telemetry

| Name | Description |
|---|---|
| `BytesSent` | Bytes handed to the transport since startup |
| `BytesReceived` | Bytes received from the transport since startup |
| `Mode` | Mode resolved from the parameters |
| `Connected` | Whether the transport is currently open |

## Events

| Name | Severity | Description |
|---|---|---|
| `ConfigurationApplied` | activity high | The parameters resolved to a usable configuration |
| `TransportNotConfigured` | warning high | `TRANSPORT` is `NONE`, so the driver stays disabled |
| `UnsupportedConfiguration` | warning high | The parameters do not describe a configuration this driver can serve |
| `IgnoredConfiguration` | warning low | Parameters were supplied that do not apply to the selected transport |
| `ConfigurationChangeDeferred` | warning low | A parameter changed after configuration; a restart is needed |
| `PortOpened` | activity high | The transport opened and is ready to carry data |
| `NoBuffers` | warning high | No buffer was available to receive into |
| `SendError` | warning low | A transmission failed |
| `ReceiveError` | warning low | A reception failed |

## Requirements

| Name | Description | Validation |
|---|---|---|
| UBSD-COMP-001 | The component shall implement the ByteStreamDriver interface | inspection |
| UBSD-COMP-002 | The component shall select its transport from the TRANSPORT parameter | unit test |
| UBSD-COMP-003 | The component shall support TCP client, TCP server, UDP and serial transports | unit test |
| UBSD-COMP-004 | The component shall resolve the driver mode from the optional local and remote address/port pairs | unit test |
| UBSD-COMP-005 | The component shall emit a warning and remain disabled when the parameters describe an unsupported configuration | unit test |
| UBSD-COMP-006 | The component shall emit a warning when parameters are supplied that do not apply to the selected transport | unit test |
| UBSD-COMP-007 | The component shall reject addresses that are not dotted-quad IPv4 addresses | unit test |
| UBSD-COMP-008 | The component shall report that a parameter change after configuration takes effect on the next restart | unit test |
| UBSD-COMP-009 | The component shall provide a read thread for transports with a receive direction | unit test |
| UBSD-COMP-010 | The component shall report the local port in use, including an ephemeral one | unit test |
