"""
End-to-end test of the hybrid FPP + C++ analysis

The topology alone allows a lock-order cycle, because wiring permits
flowThing's guarded handler to call outX. The C++ shows that handler only ever
calls outY. Together they prove there is no cycle - which neither half can
establish on its own.

Copyright 2026, by the California Institute of Technology.
ALL RIGHTS RESERVED. United States Government Sponsorship acknowledged.
"""

from guarded_port_analyzer import FindingKind, GuardedPortAnalyzer
from port_flow import PortFlowMap


def test_topology_alone_reports_a_cycle(model_builder):
    """Without the C++ half, the wiring looks like a lock-order cycle"""
    analyzer = GuardedPortAnalyzer(
        topology_path=model_builder("flow_precision"), flow=PortFlowMap.empty()
    )
    analyzer.run()

    assert ("RegTest.flowThing", "RegTest.partner") in analyzer.lock_edges
    assert ("RegTest.partner", "RegTest.flowThing") in analyzer.lock_edges
    assert analyzer.findings


def test_flow_map_eliminates_the_false_positive(model_builder, flow_map_builder):
    """With the C++ half, the edge and the cycle both disappear"""
    flow = PortFlowMap(
        flow_map_builder("FlowThingComponentBaseStub.cpp", "FlowThing.cpp")
    )
    analyzer = GuardedPortAnalyzer(
        topology_path=model_builder("flow_precision"), flow=flow
    )
    analyzer.run()

    # flowThing.gIn_handler never calls outX, so it never nests partner's mutex
    assert ("RegTest.flowThing", "RegTest.partner") not in analyzer.lock_edges
    # partner's own call back into flowThing is real and must be kept
    assert ("RegTest.partner", "RegTest.flowThing") in analyzer.lock_edges
    assert analyzer.findings == []


def test_flow_map_does_not_hide_real_cycles(model_builder, flow_map_builder):
    """Precision must not come at the cost of missing a genuine hazard"""
    flow = PortFlowMap(
        flow_map_builder("FlowThingComponentBaseStub.cpp", "FlowThing.cpp")
    )
    analyzer = GuardedPortAnalyzer(
        topology_path=model_builder("synthetic_abba"), flow=flow
    )
    analyzer.run()

    # The flow map says nothing about these components, so they stay
    # conservative and the real ABBA is still reported
    assert FindingKind.ABBA in {f.kind for f in analyzer.findings}
