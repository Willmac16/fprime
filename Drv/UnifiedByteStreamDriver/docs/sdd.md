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

Two transports are local to this module:

- `Drv::SerialStream` implements `Drv::IpSocket` on top of a POSIX termios device. That
  interface is really a byte-stream endpoint interface, so one read loop serves them all.
- `Drv::BindingTcpClientSocket` is a `Drv::TcpClientSocket` that binds a local endpoint
  before it connects, which the base class does not do.

All four are held as members, which avoids dynamic allocation.

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
port, and `getLocalPort` and the `LocalPort` channel report what was assigned.
`REMOTE_ENDPOINT` is a destination, so an entirely zero one is no destination at all, and
one that is only partly zero — an address without a port, or a port without an address —
is rejected.

### Configuration

`TRANSPORT` names the transport outright rather than leaving it to be inferred:

| `TRANSPORT`  | binds `LOCAL_ENDPOINT` | uses `REMOTE_ENDPOINT`                    |
|--------------|------------------------|-------------------------------------------|
| `TCP_CLIENT` | when asked for         | required: the destination to connect to    |
| `TCP_SERVER` | always                 | unused                                     |
| `UDP`        | always                 | the destination; none means reply-to-sender|
| `SERIAL`     | no                     | no                                         |
| `NONE`       | no                     | no: the driver stays disabled              |

Rejected: `TCP_CLIENT` with no destination, a partly zero remote endpoint, `SERIAL` with no
device, a zero `RECV_BUFFER_SIZE`, a `SEND_TIMEOUT` with a microseconds field of 1000000 or
more, a `SERIAL_BAUD_RATE` this platform's termios does not define, and any settings a
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

### Changing parameters at run time

A parameter that changes while nothing is running is resolved immediately. A parameter that
changes while the read task is running is staged and the live connection is dropped: the
task picks the change up through `getSocketHandler` on its way back round its reconnect
path, and reopens on the new values. `ConfigurationReloaded` reports it.

Doing it that way — rather than stopping and restarting the task — matters twice over.
`Drv::SocketComponentHelper` asserts its tasks have never been started, so it cannot be
restarted at all. And the configuration and the sockets it configures belong to the read
task while that task is alive, so applying a change on the commanding thread would be a
data race; `m_configLock` guards the storage, and the apply happens on the read task.

A change that resolves to nothing does not take a working link down: the rejection is
reported and the link carries on as it was.

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

### In a topology

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

There is no rate group connection to make: telemetry is pushed as it changes and downsampled
by the packetizer, like any other channel.

### Standalone use

The module is self-contained: everything it needs is in this directory, and it depends only
on `Drv_Ip`, `Drv_ByteStreamDriverModel`, `Svc_Interfaces`, `Fw_Logger` and `Os` — all
framework modules any project already has. To use it in a project rather than from upstream,
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
| `SERIAL_DEVICE` | string | `""` |
| `SERIAL_BAUD_RATE` | `Drv.SerialBaudRate` | `BAUD_115200` |
| `SERIAL_PARITY` | `Drv.SerialParity` | `PARITY_NONE` |
| `SERIAL_FLOW_CONTROL` | `Drv.SerialFlowControl` | `FLOW_NONE` |
| `SERIAL_READ_TIMEOUT` | `U8`, tenths of a second | 10 |
| `RECV_BUFFER_SIZE` | `FwSizeType` | 1024 |
| `SEND_TIMEOUT` | `Drv.SendTimeout` | 1 s |

### Serial specifics

The device is opened non-canonical and raw: 8 data bits, no input translation, no echo, no
signal generation, and no software flow control unless `FLOW_SOFTWARE` asks for it — a
`0x11` or `0x13` byte in the data would otherwise stop and start the line.

`VMIN=0` with `VTIME=SERIAL_READ_TIMEOUT` makes a read on an idle line return empty after
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

`BytesSent` and `BytesRecv` match `Drv::LinuxUartDriver` in name, type and channel id, so
ground displays built for that driver work unchanged. `Transport`, `Connected` and
`LocalPort` report what the link is doing, and the remaining channels report each parameter
value in force, so the configuration can be read back without a parameter dump.

## Events

| Name | Severity | Description |
|---|---|---|
| `ConfigurationApplied` | activity high | Parameters resolved to a usable configuration |
| `TransportNotConfigured` | warning high | `TRANSPORT` is `NONE` |
| `UnsupportedConfiguration` | warning high | Parameters describe a configuration this driver cannot serve |
| `ConfigurationReloaded` | activity high | A parameter changed and the transport was rebuilt |
| `PortOpened` | activity high | Transport opened and ready |

Send and receive failures are logged through `Fw::Logger`, not evented, which is what
`Drv::Udp` and `Drv::TcpClient` do.
