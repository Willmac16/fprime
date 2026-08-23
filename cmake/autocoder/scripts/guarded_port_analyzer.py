#!/usr/bin/env python3
"""
Guarded Port Deadlock Analyzer - fprime_python_model Implementation

Analyzes an F' topology for lock-ordering hazards between guarded ports.

Every F' component instance owns exactly one guarded-port mutex
(``m_guardedPortMutex``). The generated ``*_handlerBase`` for a guarded input
port locks that mutex, calls the user handler, and only then unlocks it, so any
port the handler invokes is invoked *while the mutex is held*. When such a call
is synchronous it runs on the caller's thread and may lock a second component's
mutex, producing a nested lock acquisition.

This tool reconstructs those nested acquisitions from the topology and reports
lock-order cycles:

* ``SELF_DEADLOCK`` - one synchronous chain re-enters a mutex it already holds.
  ``Os::Mutex`` is not recursive, so this hangs unconditionally once the chain
  is taken.
* ``ABBA`` - a lock-order cycle whose edges can be driven by two different
  threads, i.e. one thread can take A-then-B while another takes B-then-A.
* ``ABBA_SINGLE_THREAD`` - a lock-order cycle all of whose edges are only
  reachable from a single thread. It cannot interleave today, but it becomes a
  real ABBA the moment a second caller is connected.

Only ``m_guardedPortMutex`` is modeled. The parameter mutex ``m_paramLock`` is a
leaf lock: the generated code always releases it before invoking an output port,
so it cannot take part in a cross-component cycle. (It *is* held across calls
into a user-supplied external-parameter delegate, which is C++ the topology does
not describe.)

Copyright 2026, by the California Institute of Technology.
ALL RIGHTS RESERVED. United States Government Sponsorship acknowledged.
"""

import sys
import argparse
import json
import logging
import traceback
from enum import Enum
from pathlib import Path
from typing import Dict, FrozenSet, List, Optional, Set, Tuple
from dataclasses import dataclass, field

sys.path.insert(0, str(Path(__file__).resolve().parent))
from port_flow import PortFlowMap
from topology_graph import STOP, Hop, InstanceInfo, PortKey, SyncKind, TopologyGraph

logger = logging.getLogger(__name__)

# Traversal bounds. A pathological topology can otherwise blow up: the search
# state includes the held-lock stack, which is exponential in the worst case.
DEFAULT_MAX_LOCK_DEPTH = 8
DEFAULT_MAX_STATES = 500000
DEFAULT_MAX_CYCLES = 50

# Attributed to a guarded entry no thread origin reaches. Unattributed means
# "could be anything", so it must widen a cycle's severity, never narrow it.
UNKNOWN_THREAD = "<unknown>"


class Severity(Enum):
    """Finding severity, ordered by ``rank``"""

    INFO = "info"
    WARNING = "warning"
    ERROR = "error"

    def __str__(self):
        return self.value

    @property
    def rank(self) -> int:
        return {"info": 0, "warning": 1, "error": 2}[self.value]


class FindingKind(Enum):
    SELF_DEADLOCK = "SELF_DEADLOCK"
    ABBA = "ABBA"
    ABBA_SINGLE_THREAD = "ABBA_SINGLE_THREAD"

    def __str__(self):
        return self.value


@dataclass
class LockEdge:
    """``holder`` is held while ``acquired`` is locked"""

    holder: str
    acquired: str
    # The guarded input port whose handler was running when the chain started
    entry: PortKey
    # Human readable hop-by-hop witness, e.g. "a.out -> b.gIn [guarded]"
    witness: List[str] = field(default_factory=list)
    threads: Set[str] = field(default_factory=set)

    @property
    def key(self) -> Tuple[str, str]:
        return (self.holder, self.acquired)


@dataclass
class Finding:
    kind: FindingKind
    severity: Severity
    cycle: List[str]
    edges: List[LockEdge]
    detail: str = ""


class GuardedPortAnalyzer:
    """Builds and checks the guarded-mutex lock-order graph of a topology"""

    def __init__(
        self,
        topology_path: Path,
        max_lock_depth: int = DEFAULT_MAX_LOCK_DEPTH,
        max_states: int = DEFAULT_MAX_STATES,
        max_cycles: int = DEFAULT_MAX_CYCLES,
        suppressions: Optional[Set[Tuple[str, str]]] = None,
        flow: Optional[PortFlowMap] = None,
    ):
        self.topology_path = topology_path
        self.flow = flow or PortFlowMap.empty()
        self.max_lock_depth = max_lock_depth
        self.max_states = max_states
        self.max_cycles = max_cycles
        self.suppressions = suppressions or set()

        self.graph = TopologyGraph(topology_path, flow=self.flow)
        # Input PortKey -> thread origins that can invoke it
        self.port_threads: Dict[str, Set[str]] = {}
        # (holder, acquired) -> representative edge
        self.lock_edges: Dict[Tuple[str, str], LockEdge] = {}
        self.findings: List[Finding] = []
        self.truncated = False

    @property
    def instances(self) -> Dict[str, InstanceInfo]:
        """Component instances, indexed by qualified name"""
        return self.graph.instances

    @property
    def connections(self) -> Dict[str, List[PortKey]]:
        """Topology connections, indexed by source port"""
        return self.graph.connections

    def _outputs_for(self, info: InstanceInfo, flow_entry: str) -> List[str]:
        """Output ports reachable from one handler, via the shared flow engine"""
        return self.graph.outputs_for(info, flow_entry)

    # ------------------------------------------------------------------
    # Thread-origin propagation
    # ------------------------------------------------------------------

    def compute_thread_origins(self) -> None:
        """Label every input port with the thread origins that can invoke it.

        An origin is a thread of control: an active or queued instance (whose
        handlers run off its own queue), or an external caller for an entry
        port nothing in the topology drives. Origins propagate along
        synchronous and guarded hops and stop at async hops, because crossing
        an async port hands the work to the target's own thread.
        """
        connected_inputs = self.graph.connected_inputs()

        origins: List[Tuple[str, PortKey, SyncKind]] = []
        for name, info in self.instances.items():
            # An active or queued instance services its own queue on its own
            # thread, so its async handlers are where that thread starts.
            if info.is_own_thread:
                for port_name, kinds in info.input_ports.items():
                    if SyncKind.ASYNC in kinds:
                        origins.append(
                            (f"<thread:{name}>", PortKey(name, port_name), SyncKind.ASYNC)
                        )

        # An input port with no incoming connection can still be driven by
        # hand-written code (a driver task, an ISR, main). Treat it as its own
        # thread so unconnected entries are not silently assumed unreachable.
        for name, info in self.instances.items():
            for port_name, kinds in info.input_ports.items():
                key = PortKey(name, port_name)
                if str(key) not in connected_inputs:
                    for kind in kinds:
                        origins.append((f"<external:{key}>", key, kind))

        for origin_label, start_port, kind in origins:
            self._propagate_origin(origin_label, start_port, kind)

    def _propagate_origin(
        self, origin: str, start_port: PortKey, start_kind: SyncKind
    ) -> None:
        """Flood one thread origin forward across synchronous hops.

        The frontier is (instance, handler) rather than just instance, so the
        flow map narrows propagation to the output ports each handler really
        calls.
        """
        start_info = self.instances.get(start_port.instance)
        if start_info is None:
            return

        seen: Set[Tuple[str, str]] = set()
        queue: List[Tuple[str, str]] = [
            (start_port.instance, flow_entry)
            for flow_entry in start_info.flow_entries(start_port.port, start_kind)
        ]

        while queue:
            instance, flow_entry = queue.pop()
            if (instance, flow_entry) in seen:
                continue
            seen.add((instance, flow_entry))
            info = self.instances.get(instance)
            if info is None:
                continue

            for out_port in self._outputs_for(info, flow_entry):
                for dest in self.connections.get(str(PortKey(instance, out_port)), []):
                    dest_info = self.instances.get(dest.instance)
                    if dest_info is None:
                        continue
                    kinds = dest_info.input_ports.get(dest.port, set())
                    for kind in kinds:
                        if kind == SyncKind.ASYNC:
                            # Purely async hop: the work continues on the
                            # target's own thread, which is seeded separately.
                            continue
                        self.port_threads.setdefault(str(dest), set()).add(origin)
                        for nxt in dest_info.flow_entries(dest.port, kind):
                            queue.append((dest.instance, nxt))

    def _entry_threads(self, entry: PortKey) -> Set[str]:
        """Thread origins that can invoke a guarded entry port"""
        return set(self.port_threads.get(str(entry), set())) or {UNKNOWN_THREAD}

    # ------------------------------------------------------------------
    # Lock-order graph
    # ------------------------------------------------------------------

    def build_lock_graph(self) -> None:
        """Walk every guarded entry point, recording nested lock acquisitions.

        The traversal itself lives in TopologyGraph. All this supplies is the
        lock policy: a guarded hop takes a mutex and descends with it held, a
        sync hop descends unchanged, and an async hop ends the chain because
        the caller's locks are released before the message is serviced.
        """
        budget = [self.max_states]
        for name in sorted(self.instances):
            info = self.instances[name]
            for port_name in info.guarded_entries():
                entry = PortKey(name, port_name)
                threads = self._entry_threads(entry)
                logger.debug(
                    f"Walking guarded entry {entry} threads={sorted(threads)}"
                )
                self.truncated |= self.graph.walk_chains(
                    entry=entry,
                    entry_kind=SyncKind.GUARDED,
                    on_hop=self._on_hop(entry, threads),
                    # State is the ordered stack of instance mutexes held
                    initial_state=(name,),
                    state_key=frozenset,
                    budget=budget,
                )

    def _on_hop(self, entry: PortKey, threads: Set[str]):
        """Build the lock policy callback for one guarded entry point"""

        def on_hop(hop: Hop):
            held: Tuple[str, ...] = hop.state

            if hop.kind == SyncKind.ASYNC:
                # The message is queued; the chain ends here and the caller's
                # locks are released before it is serviced.
                return STOP

            if hop.kind == SyncKind.SYNC:
                # Runs on this thread, takes no mutex of its own.
                return held

            # Guarded: acquires the destination instance's mutex
            if hop.dest.instance in held:
                self._record_self_deadlock(entry, hop.dest, held, hop.path, threads)
                return STOP

            for holder in held:
                self._record_edge(
                    holder, hop.dest.instance, entry, hop.path, threads
                )

            if len(held) >= self.max_lock_depth:
                logger.debug(
                    f"  Max lock depth {self.max_lock_depth} reached at {hop.dest}"
                )
                self.truncated = True
                return STOP

            return held + (hop.dest.instance,)

        return on_hop

    def _record_edge(
        self,
        holder: str,
        acquired: str,
        entry: PortKey,
        path: List[str],
        threads: Set[str],
    ) -> None:
        """Record that ``holder``'s mutex is held while ``acquired``'s is taken"""
        if (holder, acquired) in self.suppressions:
            return
        existing = self.lock_edges.get((holder, acquired))
        if existing is None:
            self.lock_edges[(holder, acquired)] = LockEdge(
                holder=holder,
                acquired=acquired,
                entry=entry,
                witness=list(path),
                threads=set(threads),
            )
        else:
            existing.threads |= threads
            # Keep the shortest witness; it is the easiest one to read
            if len(path) < len(existing.witness):
                existing.witness = list(path)
                existing.entry = entry

    def _record_self_deadlock(
        self,
        entry: PortKey,
        dest: PortKey,
        held: Tuple[str, ...],
        path: List[str],
        threads: Set[str],
    ) -> None:
        """Record a chain that re-enters a mutex it already holds"""
        if (dest.instance, dest.instance) in self.suppressions:
            return
        edge = LockEdge(
            holder=dest.instance,
            acquired=dest.instance,
            entry=entry,
            witness=list(path),
            threads=set(threads),
        )
        detail = (
            f"{dest.instance} re-enters its own guarded mutex while it is already "
            f"held (lock stack: {' -> '.join(held)}). Os::Mutex is not recursive, "
            f"so this hangs whenever the chain is taken."
        )
        for existing in self.findings:
            if (
                existing.kind == FindingKind.SELF_DEADLOCK
                and existing.cycle == [dest.instance]
            ):
                existing.edges[0].threads |= threads
                return
        self.findings.append(
            Finding(
                kind=FindingKind.SELF_DEADLOCK,
                severity=Severity.ERROR,
                cycle=[dest.instance],
                edges=[edge],
                detail=detail,
            )
        )

    # ------------------------------------------------------------------
    # Cycle detection
    # ------------------------------------------------------------------

    def _strongly_connected_components(self) -> List[List[str]]:
        """Tarjan's SCC over the lock-order graph (iterative)"""
        graph: Dict[str, List[str]] = {}
        for holder, acquired in self.lock_edges:
            graph.setdefault(holder, []).append(acquired)
            graph.setdefault(acquired, [])

        index_counter = [0]
        index: Dict[str, int] = {}
        lowlink: Dict[str, int] = {}
        stack: List[str] = []
        on_stack: Set[str] = set()
        result: List[List[str]] = []

        for root in sorted(graph):
            if root in index:
                continue
            work: List[Tuple[str, int]] = [(root, 0)]
            while work:
                node, child_idx = work[-1]
                if child_idx == 0:
                    index[node] = lowlink[node] = index_counter[0]
                    index_counter[0] += 1
                    stack.append(node)
                    on_stack.add(node)

                recursed = False
                children = graph[node]
                for i in range(child_idx, len(children)):
                    child = children[i]
                    if child not in index:
                        work[-1] = (node, i + 1)
                        work.append((child, 0))
                        recursed = True
                        break
                    if child in on_stack:
                        lowlink[node] = min(lowlink[node], index[child])
                if recursed:
                    continue

                if lowlink[node] == index[node]:
                    component = []
                    while True:
                        member = stack.pop()
                        on_stack.discard(member)
                        component.append(member)
                        if member == node:
                            break
                    result.append(component)

                work.pop()
                if work:
                    parent = work[-1][0]
                    lowlink[parent] = min(lowlink[parent], lowlink[node])

        return result

    def _elementary_cycles(self, component: List[str]) -> List[List[str]]:
        """Enumerate elementary cycles inside one SCC, shortest first"""
        members = set(component)
        adjacency: Dict[str, List[str]] = {
            node: sorted(
                acquired
                for (holder, acquired) in self.lock_edges
                if holder == node and acquired in members and acquired != node
            )
            for node in sorted(members)
        }

        cycles: List[List[str]] = []
        for start in sorted(members):
            # Only enumerate cycles whose smallest member is the start node,
            # so each elementary cycle is emitted exactly once.
            path: List[str] = [start]
            on_path = {start}

            def dfs(node: str) -> None:
                if len(cycles) >= self.max_cycles:
                    return
                for nxt in adjacency.get(node, []):
                    if nxt == start:
                        cycles.append(list(path))
                        if len(cycles) >= self.max_cycles:
                            return
                        continue
                    if nxt in on_path or nxt < start:
                        continue
                    path.append(nxt)
                    on_path.add(nxt)
                    dfs(nxt)
                    on_path.discard(nxt)
                    path.pop()

            dfs(start)
            if len(cycles) >= self.max_cycles:
                self.truncated = True
                break

        cycles.sort(key=lambda c: (len(c), c))
        return cycles

    def _classify_cycle(self, cycle: List[str]) -> Tuple[FindingKind, Severity, str]:
        """Decide whether a lock-order cycle can actually interleave.

        A deadlock needs two threads taking the locks in opposite orders. If
        every edge of the cycle is driven by the same single thread origin, the
        orders cannot interleave today.
        """
        edges = self._cycle_edges(cycle)
        thread_sets = [edge.threads for edge in edges]

        # An unattributed edge could be driven by anything, so it can always
        # supply the second thread. Never let it argue a cycle down to a
        # warning.
        distinct_possible = any(UNKNOWN_THREAD in ts for ts in thread_sets)
        for i in range(len(thread_sets)):
            if distinct_possible:
                break
            for j in range(i + 1, len(thread_sets)):
                for t1 in thread_sets[i]:
                    for t2 in thread_sets[j]:
                        if t1 != t2:
                            distinct_possible = True
                            break
                    if distinct_possible:
                        break
                if distinct_possible:
                    break
            if distinct_possible:
                break

        if distinct_possible:
            order = " -> ".join(cycle + [cycle[0]])
            return (
                FindingKind.ABBA,
                Severity.ERROR,
                f"Lock order cycle {order}. Different threads can take these "
                f"mutexes in opposite orders, so the chains can deadlock against "
                f"each other.",
            )

        common = sorted(set().union(*thread_sets)) if thread_sets else []
        order = " -> ".join(cycle + [cycle[0]])
        return (
            FindingKind.ABBA_SINGLE_THREAD,
            Severity.WARNING,
            f"Lock order cycle {order}, but every edge is only reachable from "
            f"{', '.join(common) or 'one thread'}. It cannot interleave today; it "
            f"becomes a deadlock as soon as a second caller reaches either side.",
        )

    def _cycle_edges(self, cycle: List[str]) -> List[LockEdge]:
        edges = []
        for i, node in enumerate(cycle):
            nxt = cycle[(i + 1) % len(cycle)]
            edge = self.lock_edges.get((node, nxt))
            if edge is not None:
                edges.append(edge)
        return edges

    def detect_cycles(self) -> None:
        """Turn lock-order cycles into findings"""
        for component in self._strongly_connected_components():
            if len(component) < 2:
                continue
            for cycle in self._elementary_cycles(component):
                kind, severity, detail = self._classify_cycle(cycle)
                self.findings.append(
                    Finding(
                        kind=kind,
                        severity=severity,
                        cycle=cycle,
                        edges=self._cycle_edges(cycle),
                        detail=detail,
                    )
                )

    # ------------------------------------------------------------------
    # Reporting
    # ------------------------------------------------------------------

    def format_report(self) -> str:
        lines: List[str] = []
        lines.append("=" * 72)
        lines.append("Guarded Port Deadlock Analysis")
        lines.append("=" * 72)
        lines.append("")
        guarded = {
            name: info.guarded_entries()
            for name, info in self.instances.items()
            if info.guarded_entries()
        }
        lines.append(f"Component instances:      {len(self.instances)}")
        lines.append(f"Instances w/ guarded ports: {len(guarded)}")
        lines.append(f"Lock-order edges:         {len(self.lock_edges)}")
        lines.append(f"Intra-component flow:     {self.flow.summary()}")
        lines.append(f"Findings:                 {len(self.findings)}")
        if self.truncated:
            lines.append(
                "NOTE: traversal hit a configured bound; results may be partial."
            )
        lines.append("")

        if not self.findings:
            lines.append("No guarded-port lock-order hazards found.")
            lines.append("")
            return "\n".join(lines)

        ordered = sorted(
            self.findings, key=lambda f: (-f.severity.rank, f.kind.value, f.cycle)
        )
        for i, finding in enumerate(ordered, start=1):
            lines.append("-" * 72)
            lines.append(
                f"[{i}] {finding.severity.value.upper()}: {finding.kind} "
                f"({' -> '.join(finding.cycle + [finding.cycle[0]])})"
            )
            lines.append("-" * 72)
            lines.append(f"  {finding.detail}")
            lines.append("")
            for edge in finding.edges:
                lines.append(
                    f"  While holding {edge.holder}, locks {edge.acquired}:"
                )
                lines.append(f"    entry:   {edge.entry}")
                lines.append(f"    threads: {', '.join(sorted(edge.threads))}")
                for hop in edge.witness:
                    lines.append(f"      {hop}")
                lines.append("")
        return "\n".join(lines)

    def to_json(self) -> str:
        payload = {
            "topology": str(self.topology_path),
            "truncated": self.truncated,
            "instances": {
                name: {
                    "kind": str(info.kind),
                    "guarded_entries": info.guarded_entries(),
                }
                for name, info in sorted(self.instances.items())
            },
            "lock_edges": [
                {
                    "holder": edge.holder,
                    "acquired": edge.acquired,
                    "entry": str(edge.entry),
                    "threads": sorted(edge.threads),
                    "witness": edge.witness,
                }
                for edge in sorted(
                    self.lock_edges.values(), key=lambda e: (e.holder, e.acquired)
                )
            ],
            "findings": [
                {
                    "kind": str(finding.kind),
                    "severity": str(finding.severity),
                    "cycle": finding.cycle,
                    "detail": finding.detail,
                    "edges": [
                        {
                            "holder": edge.holder,
                            "acquired": edge.acquired,
                            "entry": str(edge.entry),
                            "threads": sorted(edge.threads),
                            "witness": edge.witness,
                        }
                        for edge in finding.edges
                    ],
                }
                for finding in self.findings
            ],
        }
        return json.dumps(payload, indent=2)

    def to_dot(self) -> str:
        in_cycle = {
            edge.key for finding in self.findings for edge in finding.edges
        }
        lines = ["digraph guarded_lock_order {", '  rankdir="LR";', "  node [shape=box];"]
        for name in sorted(
            {e.holder for e in self.lock_edges.values()}
            | {e.acquired for e in self.lock_edges.values()}
        ):
            lines.append(f'  "{name}";')
        for edge in sorted(
            self.lock_edges.values(), key=lambda e: (e.holder, e.acquired)
        ):
            attrs = 'color="red", penwidth=2' if edge.key in in_cycle else ""
            suffix = f' [{attrs}]' if attrs else ""
            lines.append(f'  "{edge.holder}" -> "{edge.acquired}"{suffix};')
        lines.append("}")
        return "\n".join(lines)

    # ------------------------------------------------------------------
    # Entry point
    # ------------------------------------------------------------------

    def run(self) -> List[Finding]:
        """Run the full analysis and return the findings"""
        logger.info(f"Loading topology from: {self.topology_path}")
        self.graph.load()
        logger.info(
            f"Found {len(self.instances)} component instances, "
            f"{self.graph.connection_count} connections"
        )

        self.compute_thread_origins()
        self.build_lock_graph()
        self.detect_cycles()
        logger.info(
            f"Lock-order graph: {len(self.lock_edges)} edges, "
            f"{len(self.findings)} findings"
        )
        return self.findings


def load_suppressions(path: Path) -> Set[Tuple[str, str]]:
    """Read a suppression file of ``holder -> acquired`` lock-order pairs.

    A bare instance name suppresses that instance's self-deadlock edge. Use
    this to record a lock ordering that is enforced by construction outside the
    topology; each entry hides a real edge, so keep them commented.
    """
    suppressions: Set[Tuple[str, str]] = set()
    for raw in path.read_text().splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        if "->" in line:
            holder, acquired = (part.strip() for part in line.split("->", 1))
            if holder and acquired:
                suppressions.add((holder, acquired))
        else:
            suppressions.add((line, line))
    return suppressions


def main():
    parser = argparse.ArgumentParser(
        description=(
            "Analyze an F' topology for guarded-port lock-order hazards "
            "(ABBA deadlocks) using fprime_python_model"
        ),
        epilog="Requires FPRIME_ENABLE_JSON_MODEL_GENERATION in CMakeLists.txt",
    )
    parser.add_argument(
        "--topology-path",
        type=Path,
        required=True,
        help="Path to topology JSON files (e.g., build-dir/Deployment/Top)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="Write the text report to this file instead of stdout",
    )
    parser.add_argument("--json", type=Path, help="Write findings as JSON to this file")
    parser.add_argument(
        "--dot", type=Path, help="Write the lock-order graph as Graphviz DOT"
    )
    parser.add_argument(
        "--flow-map",
        type=Path,
        help=(
            "Flow map from component_call_graph.py, giving which output ports "
            "each handler actually calls. Without it, every handler is assumed "
            "to call every output port"
        ),
    )
    parser.add_argument(
        "--suppress",
        type=Path,
        help="File of 'holder -> acquired' lock orderings to ignore",
    )
    parser.add_argument(
        "--fail-on",
        choices=[s.value for s in Severity] + ["never"],
        default="error",
        help="Exit non-zero when a finding at or above this severity is found",
    )
    parser.add_argument(
        "--max-lock-depth",
        type=int,
        default=DEFAULT_MAX_LOCK_DEPTH,
        help=f"Maximum nested guarded locks to explore (default {DEFAULT_MAX_LOCK_DEPTH})",
    )
    parser.add_argument(
        "--max-states",
        type=int,
        default=DEFAULT_MAX_STATES,
        help=f"Maximum traversal states (default {DEFAULT_MAX_STATES})",
    )
    parser.add_argument(
        "--max-cycles",
        type=int,
        default=DEFAULT_MAX_CYCLES,
        help=f"Maximum cycles to report per lock group (default {DEFAULT_MAX_CYCLES})",
    )
    parser.add_argument(
        "--verbose", "-v", action="store_true", help="Print detailed analysis information"
    )

    args = parser.parse_args()

    log_level = logging.DEBUG if args.verbose else logging.INFO
    logging.basicConfig(level=log_level, format="%(levelname)s: %(message)s")

    if not args.topology_path.exists():
        logger.error(f"Topology path not found: {args.topology_path}")
        return 1

    suppressions = set()
    if args.suppress:
        if not args.suppress.exists():
            logger.error(f"Suppression file not found: {args.suppress}")
            return 1
        suppressions = load_suppressions(args.suppress)
        logger.info(f"Loaded {len(suppressions)} suppressions from {args.suppress}")

    flow = PortFlowMap.empty()
    if args.flow_map:
        try:
            flow = PortFlowMap.load(args.flow_map)
        except (FileNotFoundError, ValueError) as e:
            logger.error(f"Error: {e}")
            return 1
        logger.info(f"Loaded flow map: {args.flow_map}")

    analyzer = GuardedPortAnalyzer(
        topology_path=args.topology_path.resolve(),
        flow=flow,
        max_lock_depth=args.max_lock_depth,
        max_states=args.max_states,
        max_cycles=args.max_cycles,
        suppressions=suppressions,
    )

    try:
        findings = analyzer.run()
    except Exception as e:
        logger.error(f"Error: {e}")
        if args.verbose:
            traceback.print_exc()
        return 1

    report = analyzer.format_report()
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(report)
        logger.info(f"Wrote report: {args.output}")
    else:
        print(report)

    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(analyzer.to_json())
        logger.info(f"Wrote JSON: {args.json}")

    if args.dot:
        args.dot.parent.mkdir(parents=True, exist_ok=True)
        args.dot.write_text(analyzer.to_dot())
        logger.info(f"Wrote DOT: {args.dot}")

    if args.fail_on == "never":
        return 0

    threshold = Severity(args.fail_on).rank
    failing = [f for f in findings if f.severity.rank >= threshold]
    if failing:
        logger.error(
            f"{len(failing)} finding(s) at or above severity '{args.fail_on}'"
        )
    return len(failing)


if __name__ == "__main__":
    sys.exit(main())
