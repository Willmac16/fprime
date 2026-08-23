#!/usr/bin/env python3
"""
Topology Graph - shared FPP topology model for the analysis tools

Loads the ``fpp-to-json`` artifacts and exposes the two facts every topology
analysis needs:

* how each input port is dispatched - sync, guarded or async - which decides
  whether a call crosses a thread boundary and whether it takes a mutex; and
* which output port is wired to which input port.

Combined with the intra-component flow map from ``port_flow``, this is enough to
walk real call chains across a topology. The guarded-port deadlock analysis and
the queue priority analysis both build on it, so they cannot disagree about what
"async" means.

Copyright 2026, by the California Institute of Technology.
ALL RIGHTS RESERVED. United States Government Sponsorship acknowledged.
"""

import logging
from enum import Enum
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional, Set, Tuple
from dataclasses import dataclass, field

# Note: fpm_ prefixes on imports are intentional to make it clear which
# classes are from fprime_python_model vs. local definitions
from fprime_python_model.model import FprimePythonModel as fpm_FprimePythonModel
from fprime_python_model.semantics.topology import Topology as fpm_Topology
from fprime_python_model.semantics.component_instance import (
    ComponentInstance as fpm_ComponentInstance,
)
from fprime_python_model.semantics import port_instance as fpm_port_instance
from fprime_python_model.semantics import command as fpm_command
from fprime_python_model.semantics.interface_instance import (
    InterfaceComponentInstance as fpm_InterfaceComponentInstance,
)
from fprime_python_model import fpp_ast as fpm_fpp_ast
from fprime_python_model.semantics.symbol import Symbol as fpm_Symbol

from port_flow import PortFlowMap

logger = logging.getLogger(__name__)

# Returned by a hop callback to stop descending past that hop
STOP = object()

DEFAULT_MAX_STATES = 500000

JSON_AST_FILE = "fpp-ast.json"
JSON_LOCATIONS_FILE = "fpp-loc-map.json"
JSON_ANALYSIS_FILE = "fpp-analysis.json"


class SyncKind(Enum):
    """How an input port is dispatched, and therefore what it costs to call"""

    SYNC = "sync"
    GUARDED = "guarded"
    ASYNC = "async"

    def __str__(self):
        return self.value


@dataclass(frozen=True)
class PortKey:
    """A port on a specific component instance"""

    instance: str
    port: str

    def __str__(self) -> str:
        return f"{self.instance}.{self.port}"


@dataclass
class InstanceInfo:
    """The topology facts about one component instance"""

    name: str
    ci: fpm_ComponentInstance
    kind: "fpm_fpp_ast.fpp_ast.ComponentKind"
    # FPP qualified name of the component definition, e.g. Svc.BufferManager.
    # Used to look this instance up in the C++ flow map.
    component_name: str = ""
    # Input port name -> the dispatch kinds it can run under. A command recv
    # port carries several, because the kind is per-command not per-port.
    input_ports: Dict[str, Set[SyncKind]] = field(default_factory=dict)
    output_ports: List[str] = field(default_factory=list)
    # Input port name -> queue priority, for async ports
    port_priorities: Dict[str, int] = field(default_factory=dict)
    # Name of the command recv port, when the component has one
    cmd_port: Optional[str] = None
    # Command mnemonic -> dispatch kinds
    command_kinds: Dict[str, Set[SyncKind]] = field(default_factory=dict)

    @property
    def task_priority(self) -> Optional[int]:
        """Thread priority of this instance's task, if it has one"""
        return self.ci.priority

    @property
    def is_own_thread(self) -> bool:
        """Whether handlers on this instance run on a thread of their own"""
        return self.kind in (
            fpm_fpp_ast.fpp_ast.ComponentKind.ACTIVE,
            fpm_fpp_ast.fpp_ast.ComponentKind.QUEUED,
        )

    def guarded_entries(self) -> List[str]:
        """Input ports that acquire this instance's guarded mutex"""
        return sorted(
            name
            for name, kinds in self.input_ports.items()
            if SyncKind.GUARDED in kinds
        )

    def async_entries(self) -> List[str]:
        """Input ports that enqueue a message onto this instance's queue"""
        return sorted(
            name
            for name, kinds in self.input_ports.items()
            if SyncKind.ASYNC in kinds
        )

    def flow_entries(self, port_name: str, kind: SyncKind) -> List[str]:
        """Flow-map handler keys for entering this instance at ``port_name``.

        A command recv port is not one handler but many: the dispatch kind is
        declared per command, so each command has its own handler and its own
        set of output calls. Every other input port maps to a single handler.
        """
        if port_name != self.cmd_port:
            return [port_name]
        entries = [
            f"cmd:{mnemonic}"
            for mnemonic, kinds in sorted(self.command_kinds.items())
            if kind in kinds
        ]
        return entries or [port_name]


@dataclass
class Hop:
    """One call from an output port to an input port, during a chain walk"""

    # The input port the chain was entered through
    entry: PortKey
    # Handler key at the entry port, as used by the flow map
    entry_flow: str
    # The output port being invoked
    source: PortKey
    # The input port it reaches
    dest: PortKey
    # How ``dest`` is dispatched, and therefore what this hop costs
    kind: SyncKind
    # Hop-by-hop witness ending with this hop
    path: List[str]
    # Caller state carried into this hop
    state: Any = None

    def __str__(self) -> str:
        return f"{self.source} -> {self.dest} [{self.kind}]"


class TopologyGraph:
    """The dispatch and connection structure of one FPP topology"""

    def __init__(self, topology_path: Path, flow: Optional[PortFlowMap] = None):
        self.topology_path = Path(topology_path)
        self.flow = flow or PortFlowMap.empty()
        self.model: Optional[fpm_FprimePythonModel] = None
        self.topology: Optional[fpm_Topology] = None
        self.instances: Dict[str, InstanceInfo] = {}
        # str(from PortKey) -> list of destination PortKeys
        self.connections: Dict[str, List[PortKey]] = {}

    # ------------------------------------------------------------------
    # Model loading
    # ------------------------------------------------------------------

    def _validate_json_files(self, directory: Path) -> bool:
        required = [JSON_AST_FILE, JSON_LOCATIONS_FILE, JSON_ANALYSIS_FILE]
        return all((directory / f).exists() for f in required)

    def load(self) -> "TopologyGraph":
        """Load the model and build the instance and connection tables

        Raises:
            FileNotFoundError: If required JSON files are missing
            ValueError: If no topology is present in the analysis
        """
        if not self._validate_json_files(self.topology_path):
            raise FileNotFoundError(
                f"Missing required JSON files in {self.topology_path}"
            )
        self.model = fpm_FprimePythonModel(
            str(self.topology_path / JSON_AST_FILE),
            str(self.topology_path / JSON_LOCATIONS_FILE),
            str(self.topology_path / JSON_ANALYSIS_FILE),
        )

        analysis = self.model.analysis
        if not analysis.topology_map:
            raise ValueError("No topology found in analysis")
        self.topology = next(iter(analysis.topology_map.values()))
        logger.debug(f"Loaded topology: {self.topology.get_qualified_name()}")

        self._build_instances()
        self._build_connections()
        return self

    # ------------------------------------------------------------------
    # Dispatch-kind classification
    # ------------------------------------------------------------------

    def _command_sync_kinds(self, ci: fpm_ComponentInstance) -> Set[SyncKind]:
        """Dispatch kinds a component's command recv port can run under.

        The guarded mutex is taken per-command, so one cmdIn port may be async
        for one opcode and guarded for another. Param set/save commands only
        take the parameter mutex, which the generated code releases before any
        out-call, so they behave as sync for lock-ordering purposes.
        """
        kinds: Set[SyncKind] = set()
        for command in ci.component.command_map.values():
            kinds.add(self._command_kind(command))
        return kinds

    @staticmethod
    def _command_kind(command) -> SyncKind:
        if isinstance(command, fpm_command.CommandNonParam):
            if isinstance(command.kind, fpm_command.NonParamKindAsync):
                return SyncKind.ASYNC
            if isinstance(command.kind, fpm_command.NonParamKindGuarded):
                return SyncKind.GUARDED
        return SyncKind.SYNC

    def _special_input_kind(
        self, port_instance: fpm_port_instance.SpecialPortInstance
    ) -> SyncKind:
        """Dispatch kind of a special input port from its input kind"""
        input_kind = port_instance.specifier.input_kind
        if input_kind == fpm_fpp_ast.fpp_ast.SpecialInputKind.ASYNC:
            return SyncKind.ASYNC
        if input_kind == fpm_fpp_ast.fpp_ast.SpecialInputKind.GUARDED:
            return SyncKind.GUARDED
        return SyncKind.SYNC

    def _input_sync_kinds(
        self, ci: fpm_ComponentInstance, port_instance: fpm_port_instance.PortInstance
    ) -> Set[SyncKind]:
        """All dispatch kinds an input port can run under"""
        if isinstance(port_instance, fpm_port_instance.GeneralPortInstance):
            if port_instance.kind == fpm_fpp_ast.fpp_ast.GeneralKind.GUARDED_INPUT:
                return {SyncKind.GUARDED}
            if port_instance.kind == fpm_fpp_ast.fpp_ast.GeneralKind.SYNC_INPUT:
                return {SyncKind.SYNC}
            if port_instance.kind == fpm_fpp_ast.fpp_ast.GeneralKind.ASYNC_INPUT:
                return {SyncKind.ASYNC}
            return set()

        if isinstance(port_instance, fpm_port_instance.SpecialPortInstance):
            if (
                port_instance.specifier.kind
                == fpm_fpp_ast.fpp_ast.SpecialKind.COMMAND_RECV
            ):
                kinds = self._command_sync_kinds(ci)
                # A component may declare cmdIn but no commands of its own
                return kinds or {self._special_input_kind(port_instance)}
            return {self._special_input_kind(port_instance)}

        # Internal ports are always queued onto the component's own thread
        if isinstance(port_instance, fpm_port_instance.InternalPortInstance):
            return {SyncKind.ASYNC}

        return set()

    def _port_priority(
        self, port_instance: fpm_port_instance.PortInstance
    ) -> Optional[int]:
        """Queue priority declared on an input port, if any"""
        analysis = self.model.analysis
        if isinstance(port_instance, fpm_port_instance.GeneralPortInstance):
            specifier_priority = port_instance.specifier.priority
            if specifier_priority is None:
                return None
            expression = specifier_priority.data
            if isinstance(expression, fpm_fpp_ast.fpp_ast.ExprLiteralInt):
                return int(expression.value)
            if isinstance(expression, fpm_fpp_ast.fpp_ast.ExprIdent):
                value = analysis.value_map.get(specifier_priority._id)
                if value is not None and isinstance(value.value, int):
                    return value.value
            return None
        if isinstance(
            port_instance,
            (
                fpm_port_instance.SpecialPortInstance,
                fpm_port_instance.InternalPortInstance,
            ),
        ):
            priority = port_instance.priority
            return priority if isinstance(priority, int) else None
        return None

    # ------------------------------------------------------------------
    # Table construction
    # ------------------------------------------------------------------

    def _component_qualified_name(self, ci: fpm_ComponentInstance) -> str:
        """FPP qualified name of the component definition behind an instance"""
        try:
            symbol = fpm_Symbol.construct(ci.component.a_node)
            return str(self.model.analysis.get_qualified_name_from_map(symbol))
        except (AttributeError, KeyError, ValueError) as e:
            logger.debug(f"  Could not resolve component name: {e}")
            return ""

    def _command_kinds_by_mnemonic(
        self, ci: fpm_ComponentInstance
    ) -> Dict[str, Set[SyncKind]]:
        """Map each command mnemonic to the dispatch kinds it runs under"""
        result: Dict[str, Set[SyncKind]] = {}
        for command in ci.component.command_map.values():
            try:
                mnemonic = str(command.get_name())
            except (AttributeError, TypeError):
                continue
            result.setdefault(mnemonic, set()).add(self._command_kind(command))
        return result

    def _build_instances(self) -> None:
        """Index every component instance's ports by dispatch kind"""
        for interface_instance in self.topology.instance_map:
            if not isinstance(interface_instance, fpm_InterfaceComponentInstance):
                continue
            ci = interface_instance.ci
            name = str(ci.get_qualified_name())
            info = InstanceInfo(
                name=name,
                ci=ci,
                kind=ci.component.a_node[1].data.kind,
                component_name=self._component_qualified_name(ci),
            )

            for port_name, port_instance in ci.component.port_map.items():
                if port_instance.get_direction() == fpm_port_instance.Direction.OUTPUT:
                    info.output_ports.append(str(port_name))
                    continue

                kinds = self._input_sync_kinds(ci, port_instance)
                if kinds:
                    info.input_ports[str(port_name)] = kinds
                priority = self._port_priority(port_instance)
                if priority is not None:
                    info.port_priorities[str(port_name)] = priority
                if (
                    isinstance(port_instance, fpm_port_instance.SpecialPortInstance)
                    and port_instance.specifier.kind
                    == fpm_fpp_ast.fpp_ast.SpecialKind.COMMAND_RECV
                ):
                    info.cmd_port = str(port_name)

            info.command_kinds = self._command_kinds_by_mnemonic(ci)
            self.instances[name] = info
            logger.debug(
                f"  {name} ({info.kind}): {len(info.input_ports)} inputs, "
                f"{len(info.output_ports)} outputs"
            )

    def _endpoint_port_key(self, endpoint) -> Optional[PortKey]:
        """Convert a resolved endpoint into a PortKey, if it names an instance"""
        pii = endpoint.port
        interface_instance = pii.interface_instance
        if not isinstance(interface_instance, fpm_InterfaceComponentInstance):
            return None
        return PortKey(
            instance=str(interface_instance.ci.get_qualified_name()),
            port=str(pii.port_instance.get_unqualified_name()),
        )

    def _build_connections(self) -> None:
        """Index topology connections from source port to destination ports"""
        for connections in self.topology.output_connection_map.values():
            for connection in connections:
                try:
                    from_ep = connection.from_endpoint.get_underlying_endpoint()
                    to_ep = connection.to_endpoint.get_underlying_endpoint()
                except Exception as e:  # pragma: no cover - defensive
                    logger.debug(f"  Skipping unresolvable connection: {e}")
                    continue

                from_key = self._endpoint_port_key(from_ep)
                to_key = self._endpoint_port_key(to_ep)
                if from_key is None or to_key is None:
                    continue

                self.connections.setdefault(str(from_key), []).append(to_key)
                logger.debug(f"  connection {from_key} -> {to_key}")

    # ------------------------------------------------------------------
    # Queries
    # ------------------------------------------------------------------

    def outputs_for(self, info: InstanceInfo, flow_entry: str) -> List[str]:
        """Output ports reachable from one handler, via the shared flow engine"""
        return self.flow.outputs_for(
            info.component_name, flow_entry, info.output_ports
        )

    def destinations(self, instance: str, out_port: str) -> List[PortKey]:
        """Input ports wired to one output port"""
        return self.connections.get(str(PortKey(instance, out_port)), [])

    def connected_inputs(self) -> Set[str]:
        """Every input port that something in the topology drives"""
        return {str(dest) for dests in self.connections.values() for dest in dests}

    @property
    def connection_count(self) -> int:
        return sum(len(v) for v in self.connections.values())

    # ------------------------------------------------------------------
    # Chain traversal
    # ------------------------------------------------------------------

    def walk_chains(
        self,
        entry: PortKey,
        entry_kind: SyncKind,
        on_hop: Callable[[Hop], Any],
        initial_state: Any = None,
        state_key: Callable[[Any], Any] = lambda state: state,
        budget: Optional[List[int]] = None,
    ) -> bool:
        """Walk every call chain leaving one input port's handler.

        This is the one traversal both analyses share. It resolves each
        handler's real output ports through the flow map, follows the topology
        connections, and hands every resulting hop to ``on_hop``. The analysis
        supplies only the policy:

        * return ``STOP`` to stop descending past a hop, or
        * return the state to descend with (often the state unchanged).

        The deadlock analysis carries the held-lock stack as state and stops at
        async hops; the priority analysis carries nothing and stops at async
        hops after recording them. Neither re-implements the walk.

        :param entry: the input port whose handler starts the chains
        :param entry_kind: how that input port is dispatched, which selects the
            handlers behind a command recv port
        :param budget: single-element list used as a shared mutable traversal
            budget, so one budget can span many entry points
        :returns: whether traversal stopped early on the budget
        """
        info = self.instances.get(entry.instance)
        if info is None:
            return False
        if budget is None:
            budget = [DEFAULT_MAX_STATES]

        truncated = False
        for flow_entry in info.flow_entries(entry.port, entry_kind):
            label = (
                f"{entry} [{entry_kind} entry]"
                if flow_entry == entry.port
                else f"{entry} [{entry_kind} entry: {flow_entry}]"
            )
            truncated |= self._descend(
                entry=entry,
                entry_flow=flow_entry,
                instance=entry.instance,
                flow_entry=flow_entry,
                state=initial_state,
                path=[label],
                on_hop=on_hop,
                state_key=state_key,
                visited=set(),
                budget=budget,
            )
        return truncated

    def _descend(
        self,
        entry: PortKey,
        entry_flow: str,
        instance: str,
        flow_entry: str,
        state: Any,
        path: List[str],
        on_hop: Callable[[Hop], Any],
        state_key: Callable[[Any], Any],
        visited: Set[Tuple[str, str, Any]],
        budget: List[int],
    ) -> bool:
        """Depth-first descent through one component's outward calls"""
        if budget[0] <= 0:
            return True
        marker = (instance, flow_entry, state_key(state))
        if marker in visited:
            return False
        visited.add(marker)
        budget[0] -= 1

        info = self.instances.get(instance)
        if info is None:
            return False

        truncated = False
        for out_port in self.outputs_for(info, flow_entry):
            source = PortKey(instance, out_port)
            for dest in self.destinations(instance, out_port):
                dest_info = self.instances.get(dest.instance)
                if dest_info is None:
                    continue
                for kind in sorted(
                    dest_info.input_ports.get(dest.port, set()), key=str
                ):
                    hop = Hop(
                        entry=entry,
                        entry_flow=entry_flow,
                        source=source,
                        dest=dest,
                        kind=kind,
                        path=path + [f"{source} -> {dest} [{kind}]"],
                        state=state,
                    )
                    next_state = on_hop(hop)
                    if next_state is STOP:
                        continue
                    for dest_flow in dest_info.flow_entries(dest.port, kind):
                        truncated |= self._descend(
                            entry=entry,
                            entry_flow=entry_flow,
                            instance=dest.instance,
                            flow_entry=dest_flow,
                            state=next_state,
                            path=hop.path,
                            on_hop=on_hop,
                            state_key=state_key,
                            visited=visited,
                            budget=budget,
                        )
        return truncated
