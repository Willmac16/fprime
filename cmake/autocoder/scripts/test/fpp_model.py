#!/usr/bin/env python3
"""
FPP model builder for topology analysis tests

Turns a set of ``.fpp`` sources into the ``fpp-to-json`` artifacts the topology
analyzers consume, resolving the dependency closure the same way the F' build
does: ``fpp-locate-defs`` to index every definition in the repository, then
``fpp-depend`` iterated to a fixed point, then ``fpp-to-json``.

This exists so tests can wire real F' components into a topology and analyze it
without standing up a full CMake build.

Copyright 2026, by the California Institute of Technology.
ALL RIGHTS RESERVED. United States Government Sponsorship acknowledged.
"""

import subprocess
from pathlib import Path
from typing import Iterable, List, Optional, Sequence

# Directories that define component instances or topologies of their own.
# Including them wholesale collides base IDs, because alternative subtopology
# variants deliberately share an ID range.
DEFINITION_EXCLUDES = (
    "/.git/",
    "/test/",
    "/FppTestProject/",
    "/Subtopologies/",
    "/TestDeploymentsProject/",
)

MAX_CLOSURE_ITERATIONS = 20


class FppModelError(RuntimeError):
    """Raised when an FPP tool fails"""


def _run(command: Sequence[str], cwd: Path) -> subprocess.CompletedProcess:
    result = subprocess.run(
        [str(c) for c in command], cwd=str(cwd), capture_output=True, text=True
    )
    return result


def find_fprime_root(start: Optional[Path] = None) -> Path:
    """Locate the fprime repository root by walking up from this file

    Raises:
        FppModelError: If the repository root cannot be found
    """
    here = (start or Path(__file__)).resolve()
    for candidate in [here] + list(here.parents):
        if (candidate / "cmake" / "autocoder").is_dir() and (candidate / "Fw").is_dir():
            return candidate
    raise FppModelError("Could not locate the fprime repository root")


def definition_sources(root: Path) -> List[Path]:
    """Every .fpp file that only contributes definitions, never instances"""
    sources = []
    for path in sorted(root.rglob("*.fpp")):
        text = str(path)
        if any(exclude in text for exclude in DEFINITION_EXCLUDES):
            continue
        sources.append(path)
    return sources


def write_locations_file(root: Path, out_dir: Path) -> Path:
    """Index every definition in the repository into a locations file.

    The locations file must live at the repository root: ``fpp-depend`` resolves
    the paths inside it relative to the file's own directory.

    Raises:
        FppModelError: If fpp-locate-defs fails
    """
    sources = definition_sources(root)
    result = _run(["fpp-locate-defs"] + [str(s) for s in sources], cwd=root)
    if result.returncode != 0:
        raise FppModelError(f"fpp-locate-defs failed: {result.stderr.strip()}")

    locations = out_dir / "locs.fpp"
    locations.write_text(result.stdout)

    # fpp-depend resolves relative paths against the locations file, so keep a
    # copy at the root where those paths are valid.
    root_copy = root / ".fpp-analysis-test-locs.fpp"
    root_copy.write_text(result.stdout)
    return root_copy


def dependency_closure(
    root: Path, locations: Path, seeds: Iterable[Path], work_dir: Path
) -> List[Path]:
    """Iterate fpp-depend until the dependency set stops growing

    Raises:
        FppModelError: If the closure does not converge
    """
    files = {Path(s).resolve() for s in seeds}
    direct = work_dir / "direct.txt"
    missing = work_dir / "missing.txt"

    for _ in range(MAX_CLOSURE_ITERATIONS):
        _run(
            ["fpp-depend", str(locations)]
            + [str(f) for f in sorted(files)]
            + ["-d", str(direct), "-m", str(missing)],
            cwd=root,
        )
        discovered = set()
        for output in (direct, missing):
            if not output.exists():
                continue
            for line in output.read_text().splitlines():
                line = line.strip()
                if line.endswith(".fpp"):
                    discovered.add(Path(line).resolve())

        before = len(files)
        files |= discovered
        if len(files) == before:
            return sorted(files)

    raise FppModelError("Dependency closure did not converge")


def build_model(
    seeds: Iterable[Path],
    out_dir: Path,
    root: Optional[Path] = None,
) -> Path:
    """Build fpp-to-json artifacts for ``seeds`` plus their dependency closure

    :returns: the directory holding fpp-ast.json, fpp-loc-map.json and
        fpp-analysis.json

    Raises:
        FppModelError: If any FPP tool fails
    """
    root = root or find_fprime_root()
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    locations = write_locations_file(root, out_dir)
    try:
        closure = dependency_closure(root, locations, seeds, out_dir)
        result = _run(
            ["fpp-to-json", "-d", str(out_dir)] + [str(f) for f in closure], cwd=root
        )
        if result.returncode != 0:
            raise FppModelError(f"fpp-to-json failed: {result.stderr.strip()}")
    finally:
        locations.unlink(missing_ok=True)

    for required in ("fpp-ast.json", "fpp-loc-map.json", "fpp-analysis.json"):
        if not (out_dir / required).exists():
            raise FppModelError(f"fpp-to-json did not produce {required}")

    return out_dir


def fpp_tools_available() -> bool:
    """Whether the FPP command line tools are on PATH"""
    try:
        result = subprocess.run(
            ["fpp-to-json", "--help"], capture_output=True, text=True
        )
        return result.returncode == 0
    except (OSError, subprocess.SubprocessError):
        return False
