#!/usr/bin/env python3
"""
Port Flow Map - shared intra-component call flow for topology analyses

The FPP topology (via ``fpp-to-json``) describes *inter*-component flow: which
output port is wired to which input port, and how each input port is dispatched.
It says nothing about *intra*-component flow - which output ports a given input
handler actually invokes - because that lives in the C++ implementation.

``component_call_graph.py`` recovers that half with libclang and writes it out as
a flow map. This module loads that flow map and answers the one question the
topology analyses need:

    Given component C and the handler entered at input port P, which of C's
    output ports can be invoked before that handler returns?

Both the guarded-port deadlock analysis and the queue priority analysis are
driven by that question, so they share this engine and stay consistent with each
other.

When no flow map is supplied, or a handler could not be resolved precisely, the
answer degrades to "every output port". That keeps every consumer sound: the
analyses over-approximate rather than silently missing a path.

Copyright 2026, by the California Institute of Technology.
ALL RIGHTS RESERVED. United States Government Sponsorship acknowledged.
"""

import json
import logging
from pathlib import Path
from typing import Dict, List, Optional, Set

logger = logging.getLogger(__name__)

FLOW_FORMAT_VERSION = 1


class PortFlowMap:
    """Resolves component handlers to the output ports they can invoke"""

    def __init__(self, data: Optional[dict] = None):
        self.data = data or {}
        # C++-style component name -> handler entry -> record
        self.components: Dict[str, dict] = self.data.get("components", {})
        self.stats_precise = 0
        self.stats_conservative = 0

    @classmethod
    def load(cls, path: Path) -> "PortFlowMap":
        """Load a flow map produced by component_call_graph.py

        Raises:
            FileNotFoundError: If the flow map does not exist
            ValueError: If the flow map is malformed or an unsupported version
        """
        if not path.exists():
            raise FileNotFoundError(f"Flow map not found: {path}")
        try:
            data = json.loads(path.read_text())
        except json.JSONDecodeError as e:
            raise ValueError(f"Malformed flow map {path}: {e}") from e

        version = data.get("version")
        if version != FLOW_FORMAT_VERSION:
            raise ValueError(
                f"Unsupported flow map version {version!r} in {path}; "
                f"expected {FLOW_FORMAT_VERSION}"
            )
        return cls(data)

    @classmethod
    def empty(cls) -> "PortFlowMap":
        """A flow map that always answers conservatively"""
        return cls()

    @property
    def is_empty(self) -> bool:
        return not self.components

    @staticmethod
    def _cpp_name(component_qualified_name: str) -> str:
        """Convert an FPP qualified name to the C++ spelling"""
        return component_qualified_name.replace(".", "::")

    def _lookup(self, component: str, entry: str) -> Optional[dict]:
        """Find the record for one handler, if the flow map has it"""
        record = self.components.get(self._cpp_name(component))
        if record is None:
            return None
        return record.get("handlers", {}).get(entry)

    def has_component(self, component: str) -> bool:
        """Whether the flow map covers this component at all"""
        return self._cpp_name(component) in self.components

    def outputs_for(
        self,
        component: str,
        entry: str,
        all_outputs: List[str],
    ) -> List[str]:
        """Output ports reachable from one handler.

        Falls back to ``all_outputs`` whenever the answer is not known to be
        complete: no flow map, component absent, handler absent, or the handler
        was marked opaque because libclang could not resolve one of its calls.

        :param component: FPP qualified component name, e.g. ``Svc.BufferManager``
        :param entry: input port name, or ``cmd:<MNEMONIC>`` for a command
        :param all_outputs: every output port the component declares
        """
        record = self._lookup(component, entry)
        if record is None or record.get("opaque", True):
            self.stats_conservative += 1
            return list(all_outputs)

        declared = set(all_outputs)
        # Intersect with the declared ports so a stale flow map cannot invent
        # ports that the topology does not have.
        resolved = [port for port in record.get("ports", []) if port in declared]
        self.stats_precise += 1
        return resolved

    def is_precise(self, component: str, entry: str) -> bool:
        """Whether this handler resolved to an exact output port set"""
        record = self._lookup(component, entry)
        return record is not None and not record.get("opaque", True)

    def unresolved_handlers(self) -> Set[str]:
        """Handlers present in the map but marked opaque"""
        return {
            f"{component}.{entry}"
            for component, record in self.components.items()
            for entry, handler in record.get("handlers", {}).items()
            if handler.get("opaque", False)
        }

    def summary(self) -> str:
        """One-line description of how much precision was available"""
        if self.is_empty:
            return (
                "No flow map: assuming every handler may invoke every output "
                "port (sound, imprecise)"
            )
        total = self.stats_precise + self.stats_conservative
        if total == 0:
            return f"Flow map loaded: {len(self.components)} component(s)"
        pct = 100.0 * self.stats_precise / total
        return (
            f"Flow map: {self.stats_precise}/{total} handler lookups resolved "
            f"precisely ({pct:.0f}%), {self.stats_conservative} fell back to "
            f"all-outputs"
        )
