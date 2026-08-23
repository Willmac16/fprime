#!/usr/bin/env python3
"""
Queue Priority Analyzer - hybrid FPP + C++ Implementation

Finds places where urgent work stops being urgent as it crosses the topology.

An async input port hands its message to a queue. Two things decide how quickly
that message is serviced: the port's queue priority, which orders messages
within one component's queue, and the servicing instance's task priority, which
orders the threads themselves. F' orders both the same way - a larger number is
more urgent, and 0 is the least urgent.

A chain of work usually spans several queues. This tool follows each chain from
the async port that starts it, through any synchronous hops, to the next async
port that continues it, and reports the hops where urgency drops:

* ``QUEUE_PRIORITY_INVERSION`` - work re-queued onto the *same* component's
  queue at a lower priority than it arrived with. The remainder of an urgent
  chain now sits behind every lower-priority message already queued.
* ``TASK_PRIORITY_INVERSION`` - work handed to an instance whose task priority
  is lower than the sending instance's. End-to-end latency is governed by the
  least urgent task in the chain, so the high-priority front half buys nothing.

Which output ports a handler actually calls comes from the shared flow map
(``port_flow``), the same engine the guarded-port deadlock analysis uses. Both
analyses therefore agree about what a handler can reach. Without a flow map
every handler is assumed to call every output port, which is sound but reports
chains that the C++ may never take.

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
from typing import Dict, List, Optional, Set, Tuple
from dataclasses import dataclass, field

sys.path.insert(0, str(Path(__file__).resolve().parent))
from port_flow import PortFlowMap
from topology_graph import STOP, Hop, InstanceInfo, PortKey, SyncKind, TopologyGraph

logger = logging.getLogger(__name__)

# An async port with no explicit priority sits at the bottom of its queue
DEFAULT_PORT_PRIORITY = 0

DEFAULT_MAX_STATES = 500000


class Severity(Enum):
    INFO = "info"
    WARNING = "warning"
    ERROR = "error"

    def __str__(self):
        return self.value

    @property
    def rank(self) -> int:
        return {"info": 0, "warning": 1, "error": 2}[self.value]


class FindingKind(Enum):
    QUEUE_PRIORITY_INVERSION = "QUEUE_PRIORITY_INVERSION"
    TASK_PRIORITY_INVERSION = "TASK_PRIORITY_INVERSION"

    def __str__(self):
        return self.value


@dataclass
class QueueHop:
    """One async handoff: work leaves ``source`` and is queued at ``dest``"""

    source: PortKey
    dest: PortKey
    source_priority: int
    dest_priority: int
    source_task_priority: Optional[int]
    dest_task_priority: Optional[int]
    witness: List[str] = field(default_factory=list)

    @property
    def key(self) -> Tuple[str, str]:
        return (str(self.source), str(self.dest))


@dataclass
class Finding:
    kind: FindingKind
    severity: Severity
    hop: QueueHop
    detail: str = ""


class QueuePriorityAnalyzer:
    """Follows message chains across queues and reports drops in urgency"""

    def __init__(
        self,
        topology_path: Path,
        flow: Optional[PortFlowMap] = None,
        max_states: int = DEFAULT_MAX_STATES,
        suppressions: Optional[Set[Tuple[str, str]]] = None,
    ):
        self.topology_path = Path(topology_path)
        self.flow = flow or PortFlowMap.empty()
        self.max_states = max_states
        self.suppressions = suppressions or set()
        self.graph = TopologyGraph(topology_path, flow=self.flow)
        self.hops: Dict[Tuple[str, str], QueueHop] = {}
        self.findings: List[Finding] = []
        self.truncated = False

    @property
    def instances(self) -> Dict[str, InstanceInfo]:
        return self.graph.instances

    # ------------------------------------------------------------------
    # Chain discovery
    # ------------------------------------------------------------------

    def port_priority(self, info: InstanceInfo, port_name: str) -> int:
        """Queue priority of an input port, defaulting to the lowest"""
        return info.port_priorities.get(port_name, DEFAULT_PORT_PRIORITY)

    def build_queue_hops(self) -> None:
        """Find every async-to-async handoff reachable in the topology.

        The traversal lives in TopologyGraph. All this supplies is the queue
        policy: sync and guarded hops stay on the current thread and keep the
        chain's urgency, so they descend unchanged; an async hop is where
        urgency is re-decided, so it is recorded and the chain ends.
        """
        budget = [self.max_states]
        for name in sorted(self.instances):
            info = self.instances[name]
            for port_name in info.async_entries():
                entry = PortKey(name, port_name)
                priority = self.port_priority(info, port_name)
                self.truncated |= self.graph.walk_chains(
                    entry=entry,
                    entry_kind=SyncKind.ASYNC,
                    on_hop=self._on_hop(entry, priority),
                    budget=budget,
                )

    def _on_hop(self, entry: PortKey, entry_priority: int):
        """Build the queue policy callback for one async entry point"""

        def on_hop(hop: Hop):
            if hop.kind == SyncKind.ASYNC:
                self._record_hop(
                    source=entry,
                    source_priority=entry_priority,
                    dest=hop.dest,
                    dest_info=self.instances[hop.dest.instance],
                    path=hop.path,
                )
                return STOP
            # Sync and guarded hops run on this thread; the chain keeps its
            # urgency and keeps going.
            return hop.state

        return on_hop

    def _record_hop(
        self,
        source: PortKey,
        source_priority: int,
        dest: PortKey,
        dest_info: InstanceInfo,
        path: List[str],
    ) -> None:
        """Record one async handoff, keeping the shortest witness"""
        source_info = self.instances[source.instance]
        hop = QueueHop(
            source=source,
            dest=dest,
            source_priority=source_priority,
            dest_priority=self.port_priority(dest_info, dest.port),
            source_task_priority=source_info.task_priority,
            dest_task_priority=dest_info.task_priority,
            witness=list(path),
        )
        existing = self.hops.get(hop.key)
        if existing is None or len(hop.witness) < len(existing.witness):
            self.hops[hop.key] = hop

    # ------------------------------------------------------------------
    # Classification
    # ------------------------------------------------------------------

    def detect_inversions(self) -> None:
        """Turn urgency drops into findings"""
        for hop in sorted(self.hops.values(), key=lambda h: h.key):
            if (str(hop.source), str(hop.dest)) in self.suppressions:
                continue

            if hop.source.instance == hop.dest.instance:
                # Same component, same queue: the priorities are directly
                # comparable, so a drop is unambiguous.
                if hop.dest_priority < hop.source_priority:
                    self.findings.append(
                        Finding(
                            kind=FindingKind.QUEUE_PRIORITY_INVERSION,
                            severity=Severity.ERROR,
                            hop=hop,
                            detail=(
                                f"{hop.source.instance} re-queues work from "
                                f"{hop.source.port} (priority {hop.source_priority}) "
                                f"onto {hop.dest.port} (priority "
                                f"{hop.dest_priority}) on its own queue. The rest "
                                f"of the chain now waits behind every message "
                                f"queued above priority {hop.dest_priority}."
                            ),
                        )
                    )
                continue

            # Different components mean different queues, so the port
            # priorities are not comparable. The task priorities are.
            source_task = hop.source_task_priority
            dest_task = hop.dest_task_priority
            if source_task is None or dest_task is None:
                continue
            if dest_task < source_task:
                self.findings.append(
                    Finding(
                        kind=FindingKind.TASK_PRIORITY_INVERSION,
                        severity=Severity.WARNING,
                        hop=hop,
                        detail=(
                            f"{hop.source.instance} (task priority {source_task}) "
                            f"hands work to {hop.dest.instance} (task priority "
                            f"{dest_task}). End-to-end latency is set by the "
                            f"lower priority task, so the urgency upstream does "
                            f"not carry through."
                        ),
                    )
                )

    # ------------------------------------------------------------------
    # Reporting
    # ------------------------------------------------------------------

    def format_report(self) -> str:
        lines: List[str] = []
        lines.append("=" * 72)
        lines.append("Queue Priority Analysis")
        lines.append("=" * 72)
        lines.append("")
        lines.append(f"Component instances:  {len(self.instances)}")
        lines.append(f"Async queue handoffs: {len(self.hops)}")
        lines.append(f"Intra-component flow: {self.flow.summary()}")
        lines.append(f"Findings:             {len(self.findings)}")
        if self.truncated:
            lines.append(
                "NOTE: traversal hit a configured bound; results may be partial."
            )
        lines.append("")

        if not self.findings:
            lines.append("No queue priority inversions found.")
            lines.append("")
            return "\n".join(lines)

        ordered = sorted(
            self.findings,
            key=lambda f: (-f.severity.rank, f.kind.value, str(f.hop.source)),
        )
        for i, finding in enumerate(ordered, start=1):
            hop = finding.hop
            lines.append("-" * 72)
            lines.append(
                f"[{i}] {finding.severity.value.upper()}: {finding.kind} "
                f"({hop.source} -> {hop.dest})"
            )
            lines.append("-" * 72)
            lines.append(f"  {finding.detail}")
            lines.append("")
            lines.append(
                f"  queue priority: {hop.source_priority} -> {hop.dest_priority}"
            )
            lines.append(
                f"  task priority:  {hop.source_task_priority} -> "
                f"{hop.dest_task_priority}"
            )
            for step in hop.witness:
                lines.append(f"      {step}")
            lines.append("")
        return "\n".join(lines)

    def to_json(self) -> str:
        payload = {
            "topology": str(self.topology_path),
            "truncated": self.truncated,
            "hops": [
                {
                    "source": str(hop.source),
                    "dest": str(hop.dest),
                    "source_priority": hop.source_priority,
                    "dest_priority": hop.dest_priority,
                    "source_task_priority": hop.source_task_priority,
                    "dest_task_priority": hop.dest_task_priority,
                    "witness": hop.witness,
                }
                for hop in sorted(self.hops.values(), key=lambda h: h.key)
            ],
            "findings": [
                {
                    "kind": str(f.kind),
                    "severity": str(f.severity),
                    "source": str(f.hop.source),
                    "dest": str(f.hop.dest),
                    "detail": f.detail,
                    "witness": f.hop.witness,
                }
                for f in self.findings
            ],
        }
        return json.dumps(payload, indent=2)

    def run(self) -> List[Finding]:
        """Run the full analysis and return the findings"""
        logger.info(f"Loading topology from: {self.topology_path}")
        self.graph.load()
        logger.info(
            f"Found {len(self.instances)} component instances, "
            f"{self.graph.connection_count} connections"
        )

        self.build_queue_hops()
        self.detect_inversions()
        logger.info(
            f"{len(self.hops)} queue handoff(s), {len(self.findings)} finding(s)"
        )
        return self.findings


def load_suppressions(path: Path) -> Set[Tuple[str, str]]:
    """Read a suppression file of ``source.port -> dest.port`` handoffs"""
    suppressions: Set[Tuple[str, str]] = set()
    for raw in path.read_text().splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line or "->" not in line:
            continue
        source, dest = (part.strip() for part in line.split("->", 1))
        if source and dest:
            suppressions.add((source, dest))
    return suppressions


def main():
    parser = argparse.ArgumentParser(
        description=(
            "Analyze an F' topology for queue and task priority inversions "
            "across async message chains"
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
        "--flow-map",
        type=Path,
        help=(
            "Flow map from component_call_graph.py, giving which output ports "
            "each handler actually calls. Without it, every handler is assumed "
            "to call every output port"
        ),
    )
    parser.add_argument(
        "--output", type=Path, help="Write the text report here instead of stdout"
    )
    parser.add_argument("--json", type=Path, help="Write findings as JSON to this file")
    parser.add_argument(
        "--suppress",
        type=Path,
        help="File of 'source.port -> dest.port' handoffs to ignore",
    )
    parser.add_argument(
        "--fail-on",
        choices=[s.value for s in Severity] + ["never"],
        default="error",
        help="Exit non-zero when a finding at or above this severity is found",
    )
    parser.add_argument(
        "--max-states",
        type=int,
        default=DEFAULT_MAX_STATES,
        help=f"Maximum traversal states (default {DEFAULT_MAX_STATES})",
    )
    parser.add_argument(
        "--verbose", "-v", action="store_true", help="Print detailed information"
    )

    args = parser.parse_args()
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(levelname)s: %(message)s",
    )

    if not args.topology_path.exists():
        logger.error(f"Topology path not found: {args.topology_path}")
        return 1

    flow = PortFlowMap.empty()
    if args.flow_map:
        try:
            flow = PortFlowMap.load(args.flow_map)
        except (FileNotFoundError, ValueError) as e:
            logger.error(f"Error: {e}")
            return 1

    suppressions = set()
    if args.suppress:
        if not args.suppress.exists():
            logger.error(f"Suppression file not found: {args.suppress}")
            return 1
        suppressions = load_suppressions(args.suppress)

    analyzer = QueuePriorityAnalyzer(
        topology_path=args.topology_path.resolve(),
        flow=flow,
        max_states=args.max_states,
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
