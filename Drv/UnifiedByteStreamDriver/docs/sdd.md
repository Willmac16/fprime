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

Zero carries the meaning the transports already give it, which differs by the role the
endpoint plays.

`LOCAL_ENDPOINT` is what gets bound, and both of its fields have a wildcard:

| field | zero means |
|---|---|
| address | 0.0.0.0, bind every interface |
| port | take an ephemeral port; `getLocalPort` reports what was assigned |

So the all-zero default binds every interface on an ephemeral port. That is a bind, not a
missing one — there is no way to say "do not bind", because there is no reason to: a
sender that binds nothing is given an ephemeral port anyway, and binding it explicitly is
what lets a reply come back.

`REMOTE_ENDPOINT` is a destination, and zero addresses nothing:

| remote endpoint | means |
|---|---|
| entirely zero | no destination: TCP listens, UDP replies to the last sender |
| address and port both set | the destination to reach |
| only partly zero | rejected: neither a destination nor "no destination" |

### Configuration

`REMOTE_ENDPOINT` decides the direction of an IP link:

| `TRANSPORT` | remote | behavior |
|-------------|--------|----------|
| `TCP` | reachable | connects to it; a local endpoint asked for as well is rejected |
| `TCP` | none | listens on the local endpoint |
| `UDP` | reachable | binds the local endpoint and sends to the remote |
| `UDP` | none | binds the local endpoint and replies to the last sender |
| `SERIAL` | any | serial device; IP parameters warned, unused |
| `NONE` | any | disabled, with a warning |

A TCP link that connects out binds wherever the system puts it, so a `LOCAL_ENDPOINT`
asked for alongside a reachable remote is a request that cannot be honored and is
rejected rather than quietly dropped. Every other combination resolves.

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

### In a topology

The driver presents the same ports as the other byte stream drivers, so it drops into the
place any of them occupies. Taking the `Ref` deployment's comm driver as the shape, where
`comDriver` is a `Drv.TcpClient` wired to the CCSDS comms subtopology:

```fpp
# instances.fpp
instance comDriver: Drv.UnifiedByteStreamDriver base id 0x10025000

# topology.fpp
instance comDriver

connections Comms {
  comDriver.allocate   -> ComCcsds.Subtopology.commsBufferGetCallee
  comDriver.deallocate -> ComCcsds.Subtopology.commsBufferSendIn

  comDriver.$recv                          -> ComCcsds.Subtopology.drvReceiveIn
  ComCcsds.Subtopology.drvReceiveReturnOut -> comDriver.recvReturnIn

  ComCcsds.Subtopology.drvSendOut -> comDriver.$send
  comDriver.ready                 -> ComCcsds.Subtopology.drvConnected
}

connections RateGroups {
  # Drives the telemetry in this component. Any rate group will do.
  rateGroup1Comp.RateGroupMemberOut[3] -> comDriver.run
}
```

The startup and teardown calls go where the other driver's did:

```c++
void setupTopology(const TopologyState& state) {
    // ...
    loadParameters();   // parametersLoaded resolves the transport
    startTasks(state);
    comDriver.start();  // no-op if the parameters were rejected
}

void teardownTopology(const TopologyState& state) {
    comDriver.stop();
    (void)comDriver.join();
    // ...
}
```

Two things differ from a driver configured by the topology. The endpoint comes from the
parameter database rather than from `configure`, so a deployment that takes its endpoint
from the command line has to get those values into the parameters before `loadParameters`
runs, or accept the saved ones. And `run` is worth connecting even though nothing depends
on it: it is the only thing that emits the byte counters and the connection state.

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
| `LOCAL_ENDPOINT` | `Drv.IpEndpoint` | all zero (bind every interface, ephemeral port) |
| `REMOTE_ENDPOINT` | `Drv.IpEndpoint` | all zero (no destination) |
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
reading, waiting 10 ms between them so that a device returning an immediate zero cannot
spin the read task; `stop` asks the blocked read to return. That is why stopping a serial
link can take up to a second.

`Drv::SerialStream` also overrides `shutdown`. The `Drv::IpSocket` default closes the
descriptor when `::shutdown` fails, which it does on any tty, and it leaves
`Drv::SocketComponentHelper` holding a descriptor it goes on to close a second time. The
override asks the reader to return and leaves the close to the helper that owns it.

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
| UBSD-COMP-005 | The component shall bind the local endpoint, treating its zero fields as the wildcard address and an ephemeral port | unit test |
| UBSD-COMP-006 | The component shall resolve the direction of the link from the remote endpoint | unit test |
| UBSD-COMP-007 | The component shall warn and remain unconfigured on an unsupported configuration | unit test |
| UBSD-COMP-008 | The component shall warn when parameters do not apply to the selected transport | unit test |
| UBSD-COMP-009 | The component shall defer parameter changes made after configuration | unit test |
| UBSD-COMP-010 | The component shall reject a remote endpoint that is only partly zero | unit test |
| UBSD-COMP-011 | The component shall report the local port the transport is bound to, including an ephemeral one | unit test |
