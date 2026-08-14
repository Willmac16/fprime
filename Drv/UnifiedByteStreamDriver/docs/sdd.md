# Drv::UnifiedByteStreamDriver Component

A byte stream driver whose transport is chosen by parameters rather than by the topology.
One instance covers what `Drv::TcpClient`, `Drv::TcpServer`, `Drv::Udp` and
`Drv::LinuxUartDriver` cover individually, so moving a link between transports is a
parameter change instead of a topology change and a rebuild.

See [`Drv::ByteStreamDriver`](../../Interfaces/docs/sdd.md) for the interface and
[`Drv::Ip`](../../Ip/docs/sdd.md) for the socket implementations.

## Design

The component does `import ByteStreamDriver`, so it presents the same ports as the other
byte stream drivers. It reuses the framework's transports — `Drv::TcpClientSocket`,
`Drv::TcpServerSocket` and `Drv::UdpSocket` — driven by `Drv::SocketComponentHelper`, which
supplies the read task, the reconnect task and the open/close lifecycle.

`Drv::SerialStream` implements `Drv::IpSocket` on top of a POSIX termios device. That
interface is really a byte-stream endpoint interface, so one read loop serves all four
transports. All four are held as members, which avoids dynamic allocation.

### Endpoints

An endpoint is a `Drv::IpEndpoint`: four `U8` address octets plus a `U16` port. Typed
octets cannot express an invalid address, so there is no address parsing or validation and
a host name cannot be entered.

**A port of zero marks the endpoint as unset**, which is what makes the local and remote
endpoints optional. It also means an ephemeral port cannot be requested here; use
`Drv::TcpServer` or `Drv::Udp` directly if you need one.

### Configuration

| `TRANSPORT` | local | remote | behavior                                        |
|-------------|-------|--------|-------------------------------------------------|
| `TCP`       | set   | unset  | listens on the local endpoint                   |
| `TCP`       | unset | set    | connects to the remote endpoint                 |
| `TCP`       | set   | set    | rejected: nothing both binds and connects       |
| `TCP`       | unset | unset  | rejected: nothing to listen on or connect to    |
| `UDP`       | set   | unset  | bound locally, replying to the last sender      |
| `UDP`       | unset | set    | send-only to the remote endpoint                |
| `UDP`       | set   | set    | bound locally and sending to the remote         |
| `UDP`       | unset | unset  | rejected: nothing to bind or send to            |
| `SERIAL`    | any   | any    | serial device; IP parameters warned, unused     |
| `NONE`      | any   | any    | disabled, with a warning                        |

A combination that cannot be served emits `UnsupportedConfiguration` and leaves the driver
unconfigured — a link that silently works in one direction is worse than one that refuses
to start and says why. Parameters that do not apply to the transport emit
`IgnoredConfiguration`, a warning rather than a rejection. Also rejected: a zero
`RECV_BUFFER_SIZE`, a `SEND_TIMEOUT_MICROSECONDS` of 1000000 or more, and a
`SERIAL_BAUD_RATE` this platform's termios does not define.

Configuration resolves once, in `parametersLoaded` — or on the first `start` for
deployments with no parameter database. A later parameter change emits
`ConfigurationChangeDeferred` and applies at the next restart, because rebuilding a
transport underneath a live read task is not safe. A rejection is equally final.

## Usage

```c++
Drv::UnifiedByteStreamDriver comm("comm");

void startTasks() {
    comm.start();   // configures from parameters if that has not happened yet
}

void exitTasks() {
    comm.stop();
    (void) comm.join();
}
```

`start` is a no-op on a rejected configuration, so callers need not check first.
`getTransport` reports what resolved; `getLocalPort` reports the bound port.

### Standalone use

The module is self-contained: everything it needs is in this directory, and it depends only
on `Drv_Ip`, `Drv_ByteStreamDriverModel`, `Fw_Logger` and `Os` — all framework modules any
project already has. To use it in a project rather than from upstream, copy this directory
anywhere in the project and add `add_fprime_subdirectory` for it.

Two follow-ups if you move it out of `Drv/`. Autocoded headers are included by their path
from the build root, so the `<Drv/UnifiedByteStreamDriver/...Ac.hpp>` includes in
`UnifiedByteStreamDriver.hpp` and `SerialStream.hpp` need the new path. And the FPP puts
its types in `module Drv`; rename that module if you would rather not add to the
framework's namespace.

### Parameters

| Parameter | Type | Default |
|---|---|---|
| `TRANSPORT` | `Drv.ByteStreamTransport` | `NONE` |
| `LOCAL_ENDPOINT` | `Drv.IpEndpoint` | unset |
| `REMOTE_ENDPOINT` | `Drv.IpEndpoint` | unset |
| `SERIAL_DEVICE` | string | `""` |
| `SERIAL_BAUD_RATE` | `Drv.SerialBaudRate` | `BAUD_115200` |
| `SERIAL_PARITY` | `Drv.SerialParity` | `PARITY_NONE` |
| `SERIAL_FLOW_CONTROL` | `Drv.SerialFlowControl` | `FLOW_NONE` |
| `RECV_BUFFER_SIZE` | `FwSizeType` | 1024 |
| `SEND_TIMEOUT_SECONDS` | `U32` | 1 |
| `SEND_TIMEOUT_MICROSECONDS` | `U32` | 0 |

Parameters, telemetry and events live in `Parameters.fppi`, `Telemetry.fppi` and
`Events.fppi`.

### Serial specifics

The device is opened non-canonical, 8 data bits, `VMIN=0`/`VTIME=10`, so a read on an idle
line returns after about a second. `Drv::SerialStream` absorbs those empty reads and keeps
reading; `stop` asks the blocked read to return. That is why stopping a serial link can
take up to a second.

## Ports

| Name | Description |
|---|---|
| `send` | Guarded input; sends data out and returns a status |
| `recv` | Output; delivers received data with a status |
| `recvReturnIn` | Guarded input; returns ownership of a `recv` buffer |
| `ready` | Output; invoked when the transport opens |
| `allocate` / `deallocate` | Output; buffer management for the read task |
| `run` | Sync input; rate group tick that emits telemetry |

## Telemetry

| Name | Description |
|---|---|
| `BytesSent` | Bytes handed to the transport since startup |
| `BytesRecv` | Bytes received from the transport since startup |
| `Transport` | Transport resolved from the parameters |
| `Connected` | Whether the transport is currently open |

`BytesSent` and `BytesRecv` match `Drv::LinuxUartDriver` in name, type and channel id, so
ground displays built for that driver work unchanged.

## Events

| Name | Severity | Description |
|---|---|---|
| `ConfigurationApplied` | activity high | Parameters resolved to a usable configuration |
| `TransportNotConfigured` | warning high | `TRANSPORT` is `NONE` |
| `UnsupportedConfiguration` | warning high | Parameters describe a configuration this driver cannot serve |
| `IgnoredConfiguration` | warning low | Parameters supplied that do not apply to the transport |
| `ConfigurationChangeDeferred` | warning low | Parameter changed after configuration; restart needed |
| `PortOpened` | activity high | Transport opened and ready |
| `NoBuffers` | warning high | No buffer available to receive into |
| `SendError` | warning low | A transmission failed |
| `ReceiveError` | warning low | A reception failed |

## Requirements

| Name | Description | Validation |
|---|---|---|
| UBSD-COMP-001 | The component shall implement the Drv.ByteStreamDriver interface | inspection |
| UBSD-COMP-002 | The component shall select its transport from the TRANSPORT parameter | unit test |
| UBSD-COMP-003 | The component shall support TCP, UDP and serial transports | unit test |
| UBSD-COMP-004 | The component shall accept IP endpoints as typed address octets and a port | inspection |
| UBSD-COMP-005 | The component shall treat an endpoint with a zero port as unset | unit test |
| UBSD-COMP-006 | The component shall resolve the direction of the link from the endpoints supplied | unit test |
| UBSD-COMP-007 | The component shall warn and remain unconfigured on an unsupported configuration | unit test |
| UBSD-COMP-008 | The component shall warn when parameters do not apply to the selected transport | unit test |
| UBSD-COMP-009 | The component shall defer parameter changes made after configuration | unit test |
| UBSD-COMP-010 | The component shall provide a read thread for configurations with a receive direction | unit test |
| UBSD-COMP-011 | The component shall report the local port the transport is bound to | unit test |
