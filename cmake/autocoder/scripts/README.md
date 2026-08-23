# Topology Analysis Tools

Static analyses over an F´ topology, for concurrency defects that no single
source of truth can see on its own.

## Why the analysis is hybrid

An F´ concurrency bug lives across two artifacts:

| Question | Answered by |
| --- | --- |
| Which output port is wired to which input port? | the FPP topology (`fpp-to-json`) |
| Does that input port lock a mutex, or queue a message? | the FPP topology |
| Which output ports does *this handler* actually call? | the C++ implementation |

Neither half is sufficient. A C++-only tool cannot resolve a port call, because
`someOut_out()` dispatches through a port object wired at topology-init time —
the far end exists only in the FPP model. An FPP-only tool has to assume every
handler may call every output port, which is sound but reports chains the code
never takes.

So the topology supplies the inter-component edges, `libclang` supplies the
intra-component ones, and the analyses run over the join.

```
                 ┌─ inter-component edges ─┐
fpp-to-json ─────┤  + sync/guarded/async   ├──►  topology_graph.py   ──►  guarded_port_analyzer.py
                 └─ tagging per input port ┘     (the tagged graph,       queue_priority_analyzer.py
                                             ▲    plus the one shared     (policy only)
component_call_graph.py ──► port_flow.py ────┘    chain traversal)
   (libclang: which output ports
    a handler actually calls)
```

The layering is deliberate:

1. **`component_call_graph.py` + `port_flow.py`** — the C++ port connection
   engine. Resolves each handler to the output ports it really invokes.
2. **`topology_graph.py`** — builds the whole topology as one graph, with every
   input port tagged sync / guarded / async (and commands tagged per mnemonic),
   and owns the single chain traversal that walks it.
3. **The analyzers** — each is *only* a policy over that traversal. Every hop is
   handed to a callback that returns either the state to descend with or `STOP`.
   The deadlock analysis carries the held-lock stack and stops at async hops;
   the priority analysis carries nothing and records async hops before stopping.
   Neither re-implements the walk, so they cannot drift apart about what "async"
   means. A new analysis is a new callback, not a new traversal.

## Tools

### `guarded_port_analyzer.py` — ABBA deadlock detection

Every component instance owns exactly one guarded-port mutex
(`m_guardedPortMutex`). The generated `*_handlerBase` for a guarded input port
locks it, calls the handler, and unlocks it afterwards, so **every port the
handler invokes is invoked with that mutex held**. A synchronous call out of a
guarded handler that lands on another guarded port therefore nests two mutexes.

The tool walks every guarded entry point, carrying a lock stack, and records
each nested acquisition as a lock-order edge — the same relation a runtime
checker like lockdep learns. Cycles in that graph are reported:

| Finding | Meaning |
| --- | --- |
| `SELF_DEADLOCK` | One chain re-enters a mutex it already holds. `Os::Mutex` is not recursive, so this hangs unconditionally. |
| `ABBA` | A lock-order cycle whose edges two different threads can drive, one taking A-then-B while the other takes B-then-A. |
| `ABBA_SINGLE_THREAD` | A lock-order cycle only one thread can reach. It cannot interleave today, but becomes a real ABBA once a second caller is connected. |

Dispatch rules that drive the walk:

* **guarded** input — takes the target's mutex, chain continues on this thread
* **sync** input — takes no mutex, chain continues on this thread
* **async** input — message is queued and the caller's locks are released, so
  the chain **ends**

Commands are resolved per mnemonic, since `sync`/`guarded`/`async` is declared
per command rather than on the `command recv` port.

```bash
python3 guarded_port_analyzer.py \
    --topology-path build-fprime-automatic-native/MyDeployment/Top \
    --flow-map flow.json \
    --dot locks.dot --fail-on error
```

### `queue_priority_analyzer.py` — queue and task priority inversion

Follows each chain from the async port that starts it, through any synchronous
hops, to the next async port that continues it, and reports where urgency drops.

| Finding | Meaning |
| --- | --- |
| `QUEUE_PRIORITY_INVERSION` | Work re-queued onto the *same* component's queue at a lower priority than it arrived with. |
| `TASK_PRIORITY_INVERSION` | Work handed to an instance whose task priority is lower, capping end-to-end latency. |

Port priorities are only compared **within one queue**. Across components the
numbers order different queues and are not comparable, so only task priorities
are used there. In F´ a larger number is more urgent and 0 is the least urgent.

### `component_call_graph.py` — C++ handler → output port resolution

Builds a member-function call graph from `compile_commands.json` and computes,
for each handler, the transitive closure of what it calls and which output ports
those functions invoke:

```
bufferSendIn_handler
  -> BufferManager::returnBuffer                    (private helper)
    -> BufferManagerComponentBase::bufferDeallocate_out
      -> m_bufferDeallocate_OutputPort              (port invocation)
```

A port invocation is recognized from the generated `m_<port>_OutputPort` member
and from `<port>_out` invoker names, so telemetry, event, parameter and time
helpers resolve through the same closure without special-casing.

```bash
pip install libclang
python3 component_call_graph.py \
    --compile-commands build/compile_commands.json \
    --exclude '/test/' --output flow.json
```

**Soundness.** A handler containing a call libclang cannot resolve (through a
function pointer or a delegate) is marked `opaque`, and consumers fall back to
"may call any output port" for it. Precision never comes at the cost of missing
a hazard: without a flow map, or for any handler not in it, the analyses
over-approximate.

### `port_flow.py` — the shared engine

Loads the flow map and answers one question for both analyses: *given component
C entered at handler P, which output ports can be invoked?* Sharing it means the
deadlock analysis and the priority analysis cannot disagree about what a handler
can reach, or about what "async" means.

### `priority_buffer_analyzer.py` — per-priority buffer sizing

Generates `PriorityBufferSizes.hpp` for `Os::Generic::PriorityMemQueue`. This
one is intra-component by construction — it sizes each priority level from the
component's own async ports and commands — so it needs no flow map. See
`Os/Generic/docs/sdd.md`.

## What is deliberately not modeled

* **`m_paramLock`.** The generated code releases the parameter mutex before
  every output port call, so it is a leaf lock and cannot join a cross-component
  cycle. (It *is* held across calls into a user-supplied external-parameter
  delegate, which is C++ the topology does not describe.)
* **User-written threads and callbacks.** Only F´ port calls are followed.
* **Runtime guards.** A handler that calls a port only under a condition the
  analysis cannot evaluate is treated as though it always does.

## Suppressions

Both analyzers accept a suppression file for orderings enforced outside the
topology. Each entry hides a real edge, so keep them commented:

```
# fileUplink always takes bufferManager before tlmChan; enforced by review
myDeployment.fileUplink -> myDeployment.tlmChan
```

## Tests

```bash
cd cmake/autocoder/scripts/test && python3 -m pytest
```

The suite covers synthetic topologies that pin down one dispatch rule at a
time, topologies built from **real F´ components** (`Svc.TlmChan`,
`Svc.BufferManager`, `Svc.EventManager`), the C++ extractor, and an end-to-end
case where the flow map removes a false positive the topology alone reports.
Tests skip themselves when the FPP tool chain or `libclang` is unavailable.
