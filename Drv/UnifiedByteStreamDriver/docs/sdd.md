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

### Resolving and re-resolving

Every parameter feeds one resolution step, so `parameterUpdated` does not care which
parameter changed: it stages a resolution and decides when that resolution can happen.
`start` performs that resolution once, over whatever the parameters hold by then.

After the first resolution, a parameter that changes while nothing is running is applied
straight away. A parameter that changes while the read task is running is staged and the
live connection is dropped: the task picks the change up through `getSocketHandler` on its
way back round its reconnect path, and reopens on the new values. `ConfigurationReloaded`
reports it either way.

The transport itself can change that way too, listeners included. `readLoop` runs one pass
per transport rather than one pass per task: it brings a listening socket up before handing
control to the helper's loop and releases it after, and `parameterUpdated` disables
automatic open so that loop returns here instead of carrying on with the transport it
started with.

Each pass settles the configuration before anything binds to it. A listening socket is bound
from the values `applyConfiguration` wrote into `Drv::TcpServerSocket`, so starting one while
a change is still staged would bind the previous endpoint — the pass applies what is pending
first, and waits while a teardown on another thread means the values are not final yet.
`Nominal.TransportSwitchWhileRunning` moves a live link onto a listener, and
`Nominal.TransportSwitchReleasesListener` moves it off one and rebinds the port to prove it
was given back.

Handing the change to the read task — rather than stopping and restarting it — matters
twice over. `Drv::SocketComponentHelper` asserts its tasks have never been started, so it
cannot be restarted at all. And the configuration and the sockets it configures belong to
the read task while that task is alive, so applying a change on the commanding thread would
be a data race. The parameters, everything resolved from them, and the lock that guards
them are one `Configuration` struct for that reason; the downlink counters and the lock that
serializes telemetry across the two threads are another.

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

The endpoint comes from the parameter database and from `param set`, not from `argv`. A
deployment that needs a command-line endpoint — `Ref`'s `-a` and `-p`, say — wants
`Drv::TcpClient` or `Drv::Udp`, whose `configure` takes one directly.

### Compared with configuring `Drv::TcpClient`, `Drv::TcpServer` and `Drv::Udp`

Those components are configured by a `configure` call in the `configComponents` phase, which
is the only place their endpoint can come from, and by a `startSocketTask` call that both
starts the read task and takes the socket parameters again. Here there is no configuration
call at all: the values are parameters, and `start` takes none. Two things follow.

- `configure` takes an address as a `const char*` to parse; a parameter is typed octets, so
  an address that cannot be represented cannot be entered.
- Those components hold the configuration privately, so it cannot be seen or changed from
  the ground. Here every value is readable as telemetry and settable by command, at the cost
  of not being settable from `argv`.

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
the link is doing. The remaining channels report each parameter as set, under the parameter's
own name, so the configuration can be read back without a parameter dump. Those track the
setting rather than the transport built from it, so a change appears on them when it is made
and `ActiveTransport` follows when the driver has rebuilt on it.

## Events

| Name | Severity | Description |
|---|---|---|
| `ConfigurationApplied` | activity high | Parameters resolved to a usable configuration |
| `TransportNotConfigured` | warning high | `TRANSPORT` is `NONE` |
| `UnsupportedConfiguration` | warning high | Parameters describe a configuration this driver cannot serve |
| `ConfigurationReloaded` | activity high | A parameter changed and the transport was rebuilt |
| `PortOpened` | activity high | Transport opened and ready |
| `NoBuffers` | warning high | No buffer available to receive into, throttled |
| `SendError` | warning low | A transmission failed, throttled |
| `ReceiveError` | warning low | A reception failed, throttled |

The last three report conditions that persist and so repeat: an allocator that stays empty,
a link that will not come up. A plain `throttle n` answers that by going quiet for good
after `n` reports, which loses every later occurrence, so those three use FPP's
`throttle 1 every { seconds = 5, useconds = 0 }` — one report, then silence until the
window has passed, then reporting resumes on its own with no throttle-clear command needed.
The window is an `Fw.TimeInterval`; naming its members matters, because a bare `every 5`
is a scalar broadcast into both of them and means five seconds *and* five microseconds.

`Drv::Udp` and `Drv::TcpClient` have no events and report failures through `Fw::Logger`.
This driver events them: a text log is much harder to see from the ground than a proper
event, and a link failing is exactly what the ground needs to see. A send refused because
no transport is configured is evented too, since a disabled driver silently swallowing
sends is the failure hardest to spot.
