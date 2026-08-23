#!/usr/bin/env python3
"""
Component Call Graph Extractor - libclang Implementation

Extracts, for every F' component implementation, the set of output ports each
input-port (or command) handler can actually invoke.

The FPP topology says which output port is wired to which input port, and which
input ports take the guarded mutex. It does not say which output ports a given
handler calls - that lives in the C++ implementation, behind ordinary member
calls. Without it, any topology-level analysis has to assume every handler may
call every output port, which is sound but very imprecise.

This tool closes that gap. For each translation unit in ``compile_commands.json``
it builds a call graph over member functions, then computes, for each handler,
the transitive closure of the functions it calls and the output ports those
functions invoke:

    bufferSendIn_handler
      -> BufferManager::returnBuffer            (private helper)
        -> BufferManagerComponentBase::bufferDeallocate_out
          -> m_bufferDeallocate_OutputPort      (port invocation)

An output port invocation is recognized two ways: a reference to the generated
``m_<port>_OutputPort`` member, and a call to a generated ``<port>_out`` method.
Telemetry, event, parameter and time helpers resolve through the same closure
once the generated ``*ComponentAc.cpp`` is parsed, since those helpers are just
member functions that touch the corresponding special port member.

Soundness: a handler that makes a call libclang cannot resolve (a call through a
function pointer or a delegate) is marked ``opaque``. Consumers must fall back to
the conservative "may call any output port" assumption for opaque handlers
rather than trusting a partial answer.

Copyright 2026, by the California Institute of Technology.
ALL RIGHTS RESERVED. United States Government Sponsorship acknowledged.
"""

import sys
import argparse
import json
import logging
import re
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple
from dataclasses import dataclass, field

logger = logging.getLogger(__name__)

FLOW_FORMAT_VERSION = 1

# Generated naming conventions used to recognize port invocations
OUTPUT_PORT_MEMBER_RE = re.compile(r"^m_(?P<port>\w+)_OutputPort$")
OUTPUT_INVOKER_RE = re.compile(r"^(?P<port>\w+)_out$")

# Handler naming conventions used to recognize entry points
PORT_HANDLER_RE = re.compile(r"^(?P<port>\w+)_handler$")
CMD_HANDLER_RE = re.compile(r"^(?P<cmd>\w+)_cmdHandler$")

# Generated base classes are named <Component>ComponentBase
COMPONENT_BASE_SUFFIX = "ComponentBase"


@dataclass
class MethodInfo:
    """One member function definition and what it reaches directly"""

    cls: str
    name: str
    # (class, method) pairs called directly from this body
    calls: Set[Tuple[str, str]] = field(default_factory=set)
    # Output port names invoked directly from this body
    ports: Set[str] = field(default_factory=set)
    # True when this body contains a call libclang could not resolve
    opaque: bool = False

    @property
    def key(self) -> Tuple[str, str]:
        return (self.cls, self.name)


class CallGraphExtractor:
    """Builds a member-function call graph and resolves handler port usage"""

    def __init__(
        self,
        compile_commands: Path,
        include_pattern: Optional[str] = None,
        exclude_pattern: Optional[str] = None,
        libclang_path: Optional[str] = None,
    ):
        self.compile_commands = compile_commands
        self.include_re = re.compile(include_pattern) if include_pattern else None
        self.exclude_re = re.compile(exclude_pattern) if exclude_pattern else None
        self.libclang_path = libclang_path

        self.methods: Dict[Tuple[str, str], MethodInfo] = {}
        self.parsed_files = 0
        self.failed_files: List[str] = []
        self.diagnostic_count = 0

    # ------------------------------------------------------------------
    # libclang setup
    # ------------------------------------------------------------------

    def _load_clang(self):
        """Import clang.cindex, configuring the library path if given

        Raises:
            RuntimeError: If the clang Python bindings are unavailable
        """
        try:
            import clang.cindex as cindex
        except ImportError as e:
            raise RuntimeError(
                "The clang Python bindings are required. Install them with "
                "'pip install libclang'."
            ) from e

        if self.libclang_path:
            path = Path(self.libclang_path)
            if path.is_dir():
                cindex.Config.set_library_path(str(path))
            else:
                cindex.Config.set_library_file(str(path))
        return cindex

    # ------------------------------------------------------------------
    # Compilation database
    # ------------------------------------------------------------------

    def load_translation_units(self) -> List[Tuple[Path, List[str], Path]]:
        """Read compile_commands.json into (file, args, directory) tuples

        Raises:
            FileNotFoundError: If the compilation database is missing
            ValueError: If the compilation database is malformed
        """
        if not self.compile_commands.exists():
            raise FileNotFoundError(
                f"Compilation database not found: {self.compile_commands}"
            )

        try:
            entries = json.loads(self.compile_commands.read_text())
        except json.JSONDecodeError as e:
            raise ValueError(f"Malformed compilation database: {e}") from e

        units = []
        for entry in entries:
            source = entry.get("file")
            if not source:
                continue
            if self.include_re and not self.include_re.search(source):
                continue
            if self.exclude_re and self.exclude_re.search(source):
                continue

            directory = Path(entry.get("directory", "."))
            args = self._extract_args(entry)
            units.append((Path(source), args, directory))

        return units

    def _extract_args(self, entry: dict) -> List[str]:
        """Get compiler arguments for one entry, dropping output/compile flags"""
        if "arguments" in entry:
            raw = list(entry["arguments"])
        else:
            import shlex

            raw = shlex.split(entry.get("command", ""))

        args: List[str] = []
        skip_next = False
        for i, arg in enumerate(raw):
            if skip_next:
                skip_next = False
                continue
            # Drop the compiler binary itself
            if i == 0:
                continue
            if arg in ("-c", "-o"):
                skip_next = arg == "-o"
                continue
            if arg == entry.get("file"):
                continue
            args.append(arg)
        return args

    # ------------------------------------------------------------------
    # AST walking
    # ------------------------------------------------------------------

    def _qualified_class_name(self, cursor) -> Optional[str]:
        """Fully qualified name of a class/struct cursor, e.g. Svc::BufferManager"""
        cindex = self._cindex
        parts: List[str] = []
        node = cursor
        while node is not None and node.kind != cindex.CursorKind.TRANSLATION_UNIT:
            if node.kind in (
                cindex.CursorKind.CLASS_DECL,
                cindex.CursorKind.STRUCT_DECL,
                cindex.CursorKind.CLASS_TEMPLATE,
                cindex.CursorKind.NAMESPACE,
            ):
                if node.spelling:
                    parts.append(node.spelling)
            node = node.semantic_parent
        if not parts:
            return None
        return "::".join(reversed(parts))

    def _method_key(self, decl) -> Optional[Tuple[str, str]]:
        """Map a method declaration to a (class, method) key"""
        parent = decl.semantic_parent
        if parent is None:
            return None
        cls = self._qualified_class_name(parent)
        if cls is None:
            return None
        return (cls, decl.spelling)

    def parse_unit(self, source: Path, args: List[str], directory: Path) -> None:
        """Parse one translation unit and fold its methods into the graph"""
        cindex = self._cindex
        index = self._index

        try:
            tu = index.parse(
                str(source),
                args=args,
                options=cindex.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD,
            )
        except cindex.TranslationUnitLoadError as e:
            logger.debug(f"  Failed to parse {source}: {e}")
            self.failed_files.append(str(source))
            return

        errors = [
            d for d in tu.diagnostics if d.severity >= cindex.Diagnostic.Error
        ]
        if errors:
            self.diagnostic_count += len(errors)
            logger.debug(
                f"  {source}: {len(errors)} parse error(s); "
                f"first: {errors[0].spelling}"
            )

        self.parsed_files += 1
        self._walk_methods(tu.cursor)

    def _walk_methods(self, cursor) -> None:
        """Find every method definition in the TU and record what it reaches"""
        cindex = self._cindex
        for node in cursor.walk_preorder():
            if node.kind not in (
                cindex.CursorKind.CXX_METHOD,
                cindex.CursorKind.CONSTRUCTOR,
                cindex.CursorKind.FUNCTION_TEMPLATE,
            ):
                continue
            if not node.is_definition():
                continue
            key = self._method_key(node)
            if key is None:
                continue

            info = self.methods.get(key)
            if info is None:
                info = MethodInfo(cls=key[0], name=key[1])
                self.methods[key] = info

            self._scan_body(node, info)

    def _scan_body(self, method_cursor, info: MethodInfo) -> None:
        """Record direct calls and direct port references inside one body"""
        cindex = self._cindex
        for node in method_cursor.walk_preorder():
            if node.kind == cindex.CursorKind.MEMBER_REF_EXPR:
                match = OUTPUT_PORT_MEMBER_RE.match(node.spelling or "")
                if match:
                    info.ports.add(match.group("port"))

            elif node.kind in (
                cindex.CursorKind.CALL_EXPR,
                cindex.CursorKind.MEMBER_REF_EXPR,
            ):
                referenced = node.referenced
                if referenced is None:
                    if node.kind == cindex.CursorKind.CALL_EXPR:
                        # An unresolved call may reach anywhere; the handler
                        # can no longer be trusted as fully enumerated.
                        info.opaque = True
                    continue
                if referenced.kind not in (
                    cindex.CursorKind.CXX_METHOD,
                    cindex.CursorKind.FUNCTION_TEMPLATE,
                    cindex.CursorKind.CONSTRUCTOR,
                ):
                    continue
                callee = self._method_key(referenced)
                if callee is None:
                    continue
                info.calls.add(callee)

                # A generated <port>_out invoker names its port directly, which
                # keeps resolution working even when the generated
                # *ComponentAc.cpp is not part of the compilation database.
                match = OUTPUT_INVOKER_RE.match(referenced.spelling or "")
                if match:
                    info.ports.add(match.group("port"))

    # ------------------------------------------------------------------
    # Closure
    # ------------------------------------------------------------------

    def resolve_handler(self, key: Tuple[str, str]) -> Tuple[Set[str], bool, int]:
        """Transitively resolve one handler to the output ports it can invoke

        Returns:
            (port names, opaque, number of functions visited)
        """
        ports: Set[str] = set()
        opaque = False
        seen: Set[Tuple[str, str]] = set()
        stack = [key]

        while stack:
            current = stack.pop()
            if current in seen:
                continue
            seen.add(current)

            info = self.methods.get(current)
            if info is None:
                # Not defined in any parsed TU. Its name may still identify a
                # port invoker, which _scan_body already recorded at the call
                # site, so treat it as a leaf rather than as opaque.
                continue

            ports |= info.ports
            opaque = opaque or info.opaque
            stack.extend(info.calls - seen)

        return ports, opaque, len(seen)

    def build_flow_map(self) -> dict:
        """Resolve every handler in the graph into the output ports it reaches"""
        components: Dict[str, dict] = {}

        for key, info in sorted(self.methods.items()):
            cls, name = key

            port_match = PORT_HANDLER_RE.match(name)
            cmd_match = CMD_HANDLER_RE.match(name)
            if not port_match and not cmd_match:
                continue

            # Attribute the handler to the component, not the generated base
            component = cls
            if component.endswith(COMPONENT_BASE_SUFFIX):
                component = component[: -len(COMPONENT_BASE_SUFFIX)]

            entry = (
                port_match.group("port")
                if port_match
                else f"cmd:{cmd_match.group('cmd')}"
            )

            ports, opaque, visited = self.resolve_handler(key)

            comp_entry = components.setdefault(
                component, {"class": cls, "handlers": {}}
            )
            existing = comp_entry["handlers"].get(entry)
            if existing is None:
                comp_entry["handlers"][entry] = {
                    "ports": sorted(ports),
                    "opaque": opaque,
                    "functions_visited": visited,
                }
            else:
                # A handler defined in both the base and the implementation:
                # union the results and stay conservative about opacity.
                existing["ports"] = sorted(set(existing["ports"]) | ports)
                existing["opaque"] = existing["opaque"] or opaque
                existing["functions_visited"] += visited

            logger.debug(
                f"  {component}.{entry} -> {sorted(ports)}"
                f"{' (opaque)' if opaque else ''}"
            )

        return {
            "version": FLOW_FORMAT_VERSION,
            "compile_commands": str(self.compile_commands),
            "parsed_files": self.parsed_files,
            "failed_files": self.failed_files,
            "components": components,
        }

    # ------------------------------------------------------------------
    # Entry point
    # ------------------------------------------------------------------

    def run(self) -> dict:
        """Parse every selected translation unit and produce the flow map"""
        self._cindex = self._load_clang()
        self._index = self._cindex.Index.create()

        units = self.load_translation_units()
        logger.info(f"Parsing {len(units)} translation unit(s)")

        for source, args, directory in units:
            logger.debug(f"Parsing {source}")
            self.parse_unit(source, args, directory)

        logger.info(
            f"Parsed {self.parsed_files} file(s), "
            f"{len(self.methods)} method definition(s)"
        )
        if self.failed_files:
            logger.warning(f"{len(self.failed_files)} file(s) failed to parse")
        if self.diagnostic_count:
            logger.warning(
                f"{self.diagnostic_count} parse error(s); the flow map may be "
                f"incomplete. Check include paths in the compilation database."
            )

        return self.build_flow_map()


def main():
    parser = argparse.ArgumentParser(
        description=(
            "Extract which output ports each F' component handler invokes, "
            "by resolving the C++ call graph with libclang"
        ),
        epilog="Requires CMAKE_EXPORT_COMPILE_COMMANDS=ON and 'pip install libclang'",
    )
    parser.add_argument(
        "--compile-commands",
        type=Path,
        required=True,
        help="Path to compile_commands.json",
    )
    parser.add_argument(
        "--output", type=Path, required=True, help="Output flow map JSON path"
    )
    parser.add_argument(
        "--include",
        help="Only parse source files whose path matches this regex",
    )
    parser.add_argument(
        "--exclude",
        help="Skip source files whose path matches this regex (e.g. '/test/')",
    )
    parser.add_argument(
        "--libclang",
        help="Path to libclang shared library or its directory, if not on the default path",
    )
    parser.add_argument(
        "--verbose", "-v", action="store_true", help="Print detailed information"
    )

    args = parser.parse_args()

    log_level = logging.DEBUG if args.verbose else logging.INFO
    logging.basicConfig(level=log_level, format="%(levelname)s: %(message)s")

    extractor = CallGraphExtractor(
        compile_commands=args.compile_commands,
        include_pattern=args.include,
        exclude_pattern=args.exclude,
        libclang_path=args.libclang,
    )

    try:
        flow_map = extractor.run()
    except (RuntimeError, FileNotFoundError, ValueError) as e:
        logger.error(f"Error: {e}")
        return 1

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(flow_map, indent=2))

    handler_count = sum(
        len(c["handlers"]) for c in flow_map["components"].values()
    )
    opaque_count = sum(
        1
        for c in flow_map["components"].values()
        for h in c["handlers"].values()
        if h["opaque"]
    )
    logger.info(
        f"Wrote {args.output}: {len(flow_map['components'])} component(s), "
        f"{handler_count} handler(s), {opaque_count} opaque"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
