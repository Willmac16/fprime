# Drv::UnifiedByteStreamDriver Component

A byte stream driver whose transport is chosen by parameters rather than by the topology.
One instance covers what `Drv::TcpClient`, `Drv::TcpServer`, `Drv::Udp` and
`Drv::LinuxUartDriver` cover individually, so moving a link between transports is a
parameter change instead of a topology change and a rebuild.

See [`Drv::ByteStreamDriver`](../../Interfaces/docs/sdd.md) for the interface and
[`Drv::Ip`](../../Ip/docs/sdd.md) for the socket implementations.

## Design

The component does `import ByteStreamDriver`, so it presents the same ports as the other
byte stream drivers, and `import Svc.BufferAllocation` for the buffers its read task fills.
It reuses the framework's transports — `Drv::TcpServerSocket` and `Drv::UdpSocket` — driven
by `Drv::SocketComponentHelper`, which supplies the read task, the reconnect task and the
open/close lifecycle.

One transport is local to this module: `Drv::SerialStream`, which implements
`Drv::IpSocket` on top of a POSIX termios device. That interface is really a byte-stream
endpoint interface, so one read loop serves them all.

All four transports are held as members, which avoids dynamic allocation.

Nothing outside this directory changes. That is deliberate: the module is meant to drop
into a project repository without a patch to F´ itself, so where the framework does not do
what is needed, the module subclasses rather than edits.

### Endpoints

An endpoint is a `Drv::IpEndpoint`: four `U8` address octets plus a `U16` port. Typed
octets cannot express an invalid address, so there is no address parsing to do and a host
name cannot be entered.

Zero carries the meaning the transports already give it, which differs by role.
`LOCAL_ENDPOINT` is bound, so an address of `0.0.0.0` binds every interface and a port of
zero takes an ephemeral one; the all-zero default binds every interface on an ephemeral
port, and `getLocalPort` and the `LocalPort` channel report what was assigned. A TCP client
does not bind, so it does not read `LOCAL_ENDPOINT`.
`REMOTE_ENDPOINT` is a destination, so an entirely zero one is no destination at all, and
one that is only partly zero — an address without a port, or a port without an address —
is rejected.

### Configuration

`TRANSPORT` names the transport outright rather than leaving it to be inferred:

| `TRANSPORT`  | binds `LOCAL_ENDPOINT` | uses `REMOTE_ENDPOINT`                    |
|--------------|------------------------|-------------------------------------------|
| `TCP_CLIENT` | no                     | required: the destination to connect to    |
| `TCP_SERVER` | always                 | unused                                     |
| `UDP`        | always                 | the destination; none means reply-to-sender|
| `SERIAL`     | no                     | no                                         |
| `NONE`       | no                     | no: the driver stays disabled              |

Rejected: `TCP_CLIENT` with no destination, a partly zero remote endpoint, `SERIAL` with no
device, a zero `RECV_BUFFER_SIZE`, a `SEND_TIMEOUT` with a microseconds field of 1000000 or
more, a baud rate this platform's termios does not define, and any settings a
transport refuses. Each emits `UnsupportedConfiguration` and leaves the driver disabled
rather than half-configured. A transport that refuses its settings is reported, not
asserted on: these values come from an operator.

Parameters that do not apply to the selected transport are simply unused. They are not
worth an event.

### Parameters are external

Every parameter is `external`, so this component owns the storage. `setConfiguration`,
`setSerialConfiguration` and `setBufferConfiguration` write the same fields the parameter
database writes, which means a deployment can configure the driver from C++ without a
second copy of the values to keep in step — a `param save` saves what C++ set, and a
`param set` overrides it.

### Resolving and re-resolving

Every parameter feeds one resolution step, so `parameterUpdated` does not care which
parameter changed: it stages a resolution and decides when that resolution can happen.
`parametersLoaded` calls `parameterUpdated` for each parameter in turn, which is why staging
matters — resolving on each call would resolve once per parameter at startup. `start`
resolves the staged set once instead.

After that first resolution, a parameter that changes while nothing is running is applied
straight away. A parameter that changes while the read task is running is staged and the
live connection is dropped: the task picks the change up through `getSocketHandler` on its
way back round its reconnect path, and reopens on the new values. `ConfigurationReloaded`
reports it either way.

Handing the change to the read task — rather than stopping and restarting it — matters
twice over. `Drv::SocketComponentHelper` asserts its tasks have never been started, so it
cannot be restarted at all. And the configuration and the sockets it configures belong to
the read task while that task is alive, so applying a change on the commanding thread would
be a data race. The parameters, everything resolved from them, and the lock that guards
them are one `Configuration` struct for that reason; the downlink counters, rate limiters
and the lock that serializes telemetry and events across the two threads are another.

Those two locks are taken configuration-first where both are needed, and neither is ever
held across a call into `Drv::SocketComponentHelper`, which takes its own lock before
calling back into `getSocketHandler`. That is why `Connected` is written where the
connection state changes rather than alongside the rest of the configuration telemetry:
asking the helper whether it is open would take the helper's lock in the opposite order.

A change that resolves to nothing does not take a working link down: the rejection is
reported and the link carries on as it was.

## Usage

The driver presents the same ports as the other byte stream drivers, so it drops into the
place any of them occupies. Taking the `Ref` deployment's comm driver as the shape:

```fpp
# instances.fpp — the phases carry the driver's own lifecycle
instance comDriver: Drv.UnifiedByteStreamDriver base id 0x10025000 \
{
  phase Fpp.ToCpp.Phases.configComponents """
  // Optional: configure from C++ instead of, or ahead of, the parameter database
  {
      const U8 address[] = {127, 0, 0, 1};
      comDriver.setConfiguration(Drv::ByteStreamTransport::TCP_CLIENT,
                                 Drv::IpEndpoint(),
                                 Drv::IpEndpoint(address, 50000));
  }
  """

  phase Fpp.ToCpp.Phases.startTasks """
  comDriver.start();
  """

  phase Fpp.ToCpp.Phases.stopTasks """
  comDriver.stop();
  """

  phase Fpp.ToCpp.Phases.freeThreads """
  (void) comDriver.join();
  """
}

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
```

`start` is a no-op on a rejected configuration, so the `startTasks` phase need not check
first.

### Compared with configuring `Drv::TcpClient`, `Drv::TcpServer` and `Drv::Udp`

Those components are configured by a `configure` call in the `configComponents` phase, which
is the only place their endpoint can come from, and by a `startSocketTask` call that both
starts the read task and takes the socket parameters again. `setConfiguration` above is the
same idea in the same phase, so a deployment that has one of them already reads much the
same. Three things differ:

- `configure` takes an address as a `const char*` to parse; `setConfiguration` takes typed
  octets, so an address that cannot be represented cannot be passed.
- Those components hold the configuration privately, so a value set in C++ cannot be seen or
  changed from the ground. Here the C++ call and a `param set` write the same external
  parameter storage, and `start` takes no configuration at all.
- `configure` returns a status a deployment usually drops on the floor. Here a rejected
  configuration is an `UnsupportedConfiguration` event and a disabled driver, so the ground
  sees it whether or not the topology checked.

### Standalone use

The module is self-contained: everything it needs is in this directory, and it depends only
on `Drv_Ip`, `Drv_ByteStreamDriverModel`, `Svc_Interfaces`, `Fw_Logger`, `Utils` and `Os` —
all framework modules any project already has. To use it in a project rather than from upstream,
copy this directory anywhere in the project and add `add_fprime_subdirectory` for it.

Two follow-ups if you move it out of `Drv/`. Autocoded headers are included by their path
from the build root, so the `<Drv/UnifiedByteStreamDriver/...Ac.hpp>` includes need the new
path. And the FPP puts its types in `module Drv`; rename that module if you would rather not
add to the framework's namespace.

### Parameters

| Parameter | Type | Default |
|---|---|---|
| `TRANSPORT` | `Drv.ByteStreamTransport` | `NONE` |
| `LOCAL_ENDPOINT` | `Drv.IpEndpoint` | all zero (bind every interface, ephemeral port) |
| `REMOTE_ENDPOINT` | `Drv.IpEndpoint` | all zero (no destination) |
| `SERIAL_CONFIG` | `Drv.SerialConfig` | no device, 115200 8N1, no flow control, 1 s read timeout |
| `RECV_BUFFER_SIZE` | `FwSizeType` | 1024 |
| `SEND_TIMEOUT` | `Drv.SendTimeout` | 1 s |

The serial line settings are one `Drv.SerialConfig` — device, baud rate, parity, flow
control and read timeout — rather than five parameters, because they are only ever set and
read together, and because a line is opened on all five at once.

### Serial specifics

The device is opened non-canonical and raw: 8 data bits, no input translation, no echo, no
signal generation, and no software flow control unless `FLOW_SOFTWARE` asks for it — a
`0x11` or `0x13` byte in the data would otherwise stop and start the line.

`VMIN=0` with `VTIME=SERIAL_CONFIG.readTimeout` makes a read on an idle line return empty after
that many tenths of a second. `Drv::SerialStream` absorbs those empty reads and keeps
reading, waiting 10 ms between them so a device returning an immediate zero cannot spin the
read task. That timeout is also what bounds how long stopping a serial link takes.

Stopping a serial link does not go through `Drv::SocketComponentHelper::stop`. That would
shut the descriptor down, and `Drv::IpSocket::shutdown` closes what `::shutdown` could not
shut — every tty — without clearing the descriptor the helper still holds and later closes
again. The stop path does the same work minus that shutdown, and the read returns on its
own read timeout.

Baud rates above 230400 are not in POSIX. Linux defines them; macOS does not, so a rate this
platform's termios does not define is rejected at configuration time rather than silently
run at the wrong speed.

## Ports

| Name | Description |
|---|---|
| `send` | Guarded input; sends data out and returns a status |
| `recv` | Output; delivers received data with a status |
| `recvReturnIn` | Guarded input; returns ownership of a `recv` buffer |
| `ready` | Output; invoked when the transport opens |
| `allocate` / `deallocate` | `Svc.BufferAllocation`, for the read task |

## Telemetry

`BytesSent` and `BytesRecv` match `Drv::LinuxUartDriver` in name and type, and are declared
first so that channel id allocation gives them the same ids, meaning ground displays built
for that driver work unchanged. `ActiveTransport`, `Connected` and `LocalPort` report what
the link is doing. The remaining channels report each parameter value in force, under the
parameter's own name, so the configuration can be read back without a parameter dump.

## Events

| Name | Severity | Description |
|---|---|---|
| `ConfigurationApplied` | activity high | Parameters resolved to a usable configuration |
| `TransportNotConfigured` | warning high | `TRANSPORT` is `NONE` |
| `UnsupportedConfiguration` | warning high | Parameters describe a configuration this driver cannot serve |
| `ConfigurationReloaded` | activity high | A parameter changed and the transport was rebuilt |
| `PortOpened` | activity high | Transport opened and ready |
| `NoBuffers` | warning high | No buffer available to receive into, rate limited |
| `SendError` | warning low | A transmission failed, rate limited |
| `ReceiveError` | warning low | A reception failed, rate limited |

The last three report conditions that persist and so repeat: an allocator that stays empty,
a link that will not come up. A count-based `throttle` answers that by going quiet for good,
which loses the report of the next occurrence, so those three go through
`Utils::RateLimiter` on a time cycle instead — the first report goes out, and the next one
waits for the window to pass.

`Drv::Udp` and `Drv::TcpClient` have no events and report failures through `Fw::Logger`.
This driver events them: a text log is much harder to see from the ground than a proper
event, and a link failing is exactly what the ground needs to see. A send refused because
no transport is configured is evented too, since a disabled driver silently swallowing
sends is the failure hardest to spot.
