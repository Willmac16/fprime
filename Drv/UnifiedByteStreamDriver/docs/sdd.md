# Drv::UnifiedByteStreamDriver Component

`Drv::UnifiedByteStreamDriver` is a byte stream driver whose transport is chosen by
parameters rather than by the topology. One instance covers what `Drv::TcpClient`,
`Drv::TcpServer`, `Drv::Udp` and `Drv::LinuxUartDriver` each cover individually, so moving
a link from, say, a UDP socket to a serial line is a parameter change rather than a
topology change and a rebuild.

For the interface this component implements, see
[`Drv::ByteStreamDriver`](../../Interfaces/docs/sdd.md) and
[`Drv::ByteStreamDriverModel`](../../ByteStreamDriverModel/docs/sdd.md). For the underlying
socket implementations, see [`Drv::Ip`](../../Ip/docs/sdd.md).

## Design

The component implements the `Drv.ByteStreamDriver` FPP interface with a plain
`import ByteStreamDriver`, so it presents exactly the ports the framework's other byte
stream drivers do: `send` in, `recv` and `ready` out, and `recvReturnIn` to take buffers
back.

It reuses the framework's existing transport machinery rather than reimplementing it. The
transports are `Drv::TcpClientSocket`, `Drv::TcpServerSocket` and `Drv::UdpSocket` — the
same classes `Drv::TcpClient`, `Drv::TcpServer` and `Drv::Udp` use — and they are driven
by `Drv::SocketComponentHelper`, which supplies the read task, the reconnect task, and the
open/close lifecycle shared with those components.

The serial transport reaches that same machinery through `Drv::SerialStream`, which
implements the `Drv::IpSocket` interface — really a byte-stream endpoint interface of
`openProtocol`/`sendProtocol`/`recvProtocol` plus close and shutdown — on top of a POSIX
termios device. That is what lets one component drive TCP, UDP and serial through exactly
the same read loop. The cost is that `Drv::SocketIpStatus` also carries non-socket errors
for the serial case; the mapping is documented in `SerialStream.hpp`.

Holding all four transports as members costs a few hundred bytes and avoids dynamic
allocation, which matters more.

### Endpoints

An endpoint is a `Drv::IpEndpoint`: four `U8` address octets in dotted-quad order plus a
`U16` port. Typed octets cannot express an invalid address, so the driver has no address
parsing or validation to do — a host name simply cannot be entered, and the dotted-quad
string the socket layer wants is built from the octets.

**A port of zero marks the endpoint as unset.** That is what makes the local and remote
endpoints optional. It also means an ephemeral port cannot be requested through this
type; a deployment that needs one should use `Drv::TcpServer` or `Drv::Udp` directly.

### Resolving the configuration

`TRANSPORT` selects the transport, and which endpoints are set decides the direction of
the link.

| `TRANSPORT` | local | remote | behavior                                             |
|-------------|-------|--------|------------------------------------------------------|
| `TCP`       | set   | unset  | listens on the local endpoint                        |
| `TCP`       | unset | set    | connects to the remote endpoint                      |
| `TCP`       | set   | set    | rejected: no transport here both binds and connects  |
| `TCP`       | unset | unset  | rejected: nothing to listen on or connect to         |
| `UDP`       | set   | unset  | bound locally, replying to the last sender           |
| `UDP`       | unset | set    | send-only to the remote endpoint                     |
| `UDP`       | set   | set    | bound locally and sending to the remote endpoint     |
| `UDP`       | unset | unset  | rejected: nothing to bind or send to                 |
| `SERIAL`    | any   | any    | serial device; the IP parameters are warned, unused  |
| `NONE`      | any   | any    | disabled, with a warning                             |

A combination that cannot be served emits `UnsupportedConfiguration` and leaves the driver
disabled. It does not come up half-configured: a link that silently works in only one
direction is worse than one that refuses to start and says why. Parameters that do not
apply to the selected transport emit `IgnoredConfiguration`, which is a warning rather
than a rejection — the link still comes up.

These further checks are applied before any transport is touched:

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
warning it emitted is the whole answer, and `start` on a rejected configuration runs no
tasks.

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

`start` is a no-op when the parameters were rejected, so no caller needs to check first.
`getTransport` reports what the parameters resolved to, and `getLocalPort` reports the port
the transport is actually bound to.

### Parameters

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `TRANSPORT` | `Drv.ByteStreamTransport` | `NONE` | Transport to use |
| `LOCAL_ENDPOINT` | `Drv.IpEndpoint` | unset | Address to bind to and port to bind it on; a port of zero leaves it unset |
| `REMOTE_ENDPOINT` | `Drv.IpEndpoint` | unset | Address to talk to and port to talk to it on; a port of zero leaves it unset |
| `SERIAL_DEVICE` | string | `""` | Serial device path, e.g. `/dev/ttyUSB0` |
| `SERIAL_BAUD_RATE` | `Drv.SerialBaudRate` | `BAUD_115200` | Baud rate of the line |
| `SERIAL_PARITY` | `Drv.SerialParity` | `PARITY_NONE` | Parity of the line |
| `SERIAL_FLOW_CONTROL` | `Drv.SerialFlowControl` | `FLOW_NONE` | Flow control of the line |
| `RECV_BUFFER_SIZE` | `FwSizeType` | 1024 | Size of the buffers allocated for receiving |
| `SEND_TIMEOUT_SECONDS` | `U32` | 1 | Seconds component of the transmit timeout |
| `SEND_TIMEOUT_MICROSECONDS` | `U32` | 0 | Microseconds component of the transmit timeout |

The parameters, telemetry channels and events live in `Parameters.fppi`, `Telemetry.fppi`
and `Events.fppi`, which the component model includes.

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
| `BytesRecv` | Bytes received from the transport since startup |
| `Transport` | Transport resolved from the parameters |
| `Connected` | Whether the transport is currently open |

`BytesSent` and `BytesRecv` match `Drv::LinuxUartDriver` in name, type and channel id, so
ground displays built for that driver work unchanged against this one.

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
| UBSD-COMP-001 | The component shall implement the Drv.ByteStreamDriver interface | inspection |
| UBSD-COMP-002 | The component shall select its transport from the TRANSPORT parameter | unit test |
| UBSD-COMP-003 | The component shall support TCP, UDP and serial transports | unit test |
| UBSD-COMP-004 | The component shall accept its IP endpoints as typed address octets and a port | inspection |
| UBSD-COMP-005 | The component shall treat an endpoint with a zero port as unset | unit test |
| UBSD-COMP-006 | The component shall resolve the direction of the link from the endpoints supplied | unit test |
| UBSD-COMP-007 | The component shall emit a warning and remain unconfigured when the parameters describe an unsupported configuration | unit test |
| UBSD-COMP-008 | The component shall emit a warning when parameters are supplied that do not apply to the selected transport | unit test |
| UBSD-COMP-009 | The component shall report that a parameter change after configuration takes effect on the next restart | unit test |
| UBSD-COMP-010 | The component shall provide a read thread for configurations with a receive direction | unit test |
| UBSD-COMP-011 | The component shall report the local port the transport is bound to | unit test |
