"""
Regression tests for the C++ component call graph extractor

The extractor answers the question the FPP model cannot: which output ports does
a given handler actually invoke? These tests pin down transitive resolution
through private helpers and through generated helpers, and the soundness
fallback for calls libclang cannot resolve.

Copyright 2026, by the California Institute of Technology.
ALL RIGHTS RESERVED. United States Government Sponsorship acknowledged.
"""

import pytest

from port_flow import PortFlowMap


def handlers_of(flow_map, component):
    return flow_map["components"][component]["handlers"]


def test_handler_resolves_through_private_helpers(flow_map_builder):
    """gIn_handler reaches alphaOut through two levels of private helper"""
    flow_map = flow_map_builder("TestComponentBaseStub.cpp", "TestThing.cpp")
    handlers = handlers_of(flow_map, "Svc::TestThing")

    assert "alphaOut" in handlers["gIn"]["ports"]
    assert not handlers["gIn"]["opaque"]


def test_handler_resolves_generated_telemetry_helper(flow_map_builder):
    """A generated tlmWrite_* helper resolves to the telemetry output port"""
    flow_map = flow_map_builder("TestComponentBaseStub.cpp", "TestThing.cpp")
    handlers = handlers_of(flow_map, "Svc::TestThing")

    assert "tlmOut" in handlers["gIn"]["ports"]


def test_handler_excludes_ports_it_never_calls(flow_map_builder):
    """The whole point: a handler is not credited with unrelated output ports"""
    flow_map = flow_map_builder("TestComponentBaseStub.cpp", "TestThing.cpp")
    handlers = handlers_of(flow_map, "Svc::TestThing")

    assert "betaOut" not in handlers["gIn"]["ports"]
    assert handlers["sIn"]["ports"] == ["betaOut"]


def test_command_handlers_are_keyed_separately(flow_map_builder):
    """Command handlers are keyed cmd:<MNEMONIC>, since kind is per command"""
    flow_map = flow_map_builder("TestComponentBaseStub.cpp", "TestThing.cpp")
    handlers = handlers_of(flow_map, "Svc::TestThing")

    assert "cmd:NOOP" in handlers
    assert handlers["cmd:NOOP"]["ports"] == ["eventOut"]


# ----------------------------------------------------------------------
# The shared flow engine
# ----------------------------------------------------------------------


def test_flow_map_narrows_outputs(flow_map_builder):
    flow = PortFlowMap(flow_map_builder("TestComponentBaseStub.cpp", "TestThing.cpp"))
    declared = ["alphaOut", "betaOut", "tlmOut", "eventOut"]

    assert set(flow.outputs_for("Svc.TestThing", "gIn", declared)) == {
        "alphaOut",
        "tlmOut",
    }
    assert flow.is_precise("Svc.TestThing", "gIn")


def test_unknown_handler_falls_back_to_all_outputs(flow_map_builder):
    """An unmapped handler must widen to every output port, never narrow"""
    flow = PortFlowMap(flow_map_builder("TestComponentBaseStub.cpp", "TestThing.cpp"))
    declared = ["alphaOut", "betaOut"]

    assert flow.outputs_for("Svc.TestThing", "notAHandler", declared) == declared
    assert flow.outputs_for("Svc.NotAComponent", "gIn", declared) == declared
    assert not flow.is_precise("Svc.TestThing", "notAHandler")


def test_empty_flow_map_is_fully_conservative():
    flow = PortFlowMap.empty()
    declared = ["a", "b", "c"]

    assert flow.is_empty
    assert flow.outputs_for("Any.Component", "anyHandler", declared) == declared


def test_flow_map_cannot_invent_undeclared_ports(flow_map_builder):
    """A stale flow map must not add ports the topology does not declare"""
    flow = PortFlowMap(flow_map_builder("TestComponentBaseStub.cpp", "TestThing.cpp"))

    # alphaOut is resolved by the extractor but absent from the declared list
    assert flow.outputs_for("Svc.TestThing", "gIn", ["tlmOut"]) == ["tlmOut"]


def test_rejects_unsupported_version(tmp_path):
    import json

    path = tmp_path / "flow.json"
    path.write_text(json.dumps({"version": 99, "components": {}}))

    with pytest.raises(ValueError, match="Unsupported flow map version"):
        PortFlowMap.load(path)
