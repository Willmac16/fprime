"""
Regression tests for the queue priority analyzer

Copyright 2026, by the California Institute of Technology.
ALL RIGHTS RESERVED. United States Government Sponsorship acknowledged.
"""

from queue_priority_analyzer import FindingKind, QueuePriorityAnalyzer, Severity
from port_flow import PortFlowMap


def analyze(model_dir, **kwargs):
    analyzer = QueuePriorityAnalyzer(topology_path=model_dir, **kwargs)
    analyzer.run()
    return analyzer


def findings_by_kind(analyzer, kind):
    return [f for f in analyzer.findings if f.kind == kind]


def test_same_queue_priority_drop_is_an_error(model_builder):
    """Re-queuing onto a lower priority port of the same queue is unambiguous"""
    analyzer = analyze(model_builder("priority_inversion"))

    findings = findings_by_kind(analyzer, FindingKind.QUEUE_PRIORITY_INVERSION)
    assert len(findings) == 1

    finding = findings[0]
    assert finding.severity == Severity.ERROR
    assert str(finding.hop.source) == "T.urgent.hiIn"
    assert str(finding.hop.dest) == "T.urgent.loIn"
    assert (finding.hop.source_priority, finding.hop.dest_priority) == (9, 1)


def test_task_priority_drop_is_reported(model_builder):
    """Handing work to a lower priority task caps the chain's urgency"""
    analyzer = analyze(model_builder("priority_inversion"))

    findings = findings_by_kind(analyzer, FindingKind.TASK_PRIORITY_INVERSION)
    assert findings
    assert all(f.severity == Severity.WARNING for f in findings)
    assert all(f.hop.dest.instance == "T.sluggish" for f in findings)


def test_priority_increase_is_not_reported(model_builder):
    """Handing work to a higher priority task is not an inversion"""
    analyzer = analyze(model_builder("priority_inversion"))

    assert all(f.hop.dest.instance != "T.peer" for f in analyzer.findings)
    # The handoff is still recorded, it is just not a finding
    assert ("T.urgent.hiIn", "T.peer.workIn") in analyzer.hops


def test_cross_queue_port_priorities_are_not_compared(model_builder):
    """Port priorities on different queues are not comparable

    urgent.loIn is priority 1 and sluggish.workIn is priority 5. Those numbers
    order messages within different queues, so a naive comparison would invent
    an inversion here. Only the task priorities are comparable.
    """
    analyzer = analyze(model_builder("priority_inversion"))

    hop = analyzer.hops[("T.urgent.loIn", "T.sluggish.workIn")]
    assert (hop.source_priority, hop.dest_priority) == (1, 5)
    # Reported because of the task priority drop, not the port priorities
    findings = [
        f
        for f in analyzer.findings
        if f.hop.key == ("T.urgent.loIn", "T.sluggish.workIn")
    ]
    assert [f.kind for f in findings] == [FindingKind.TASK_PRIORITY_INVERSION]


def test_async_chain_stops_at_the_queue(model_builder):
    """A chain ends at the async hop; urgency is re-decided there"""
    analyzer = analyze(model_builder("synthetic_async_break"))

    # a.gIn is guarded, not async, so it is not a chain start; b.aIn is
    assert all(hop.source.port == "aIn" for hop in analyzer.hops.values())


def test_suppression_removes_a_handoff(model_builder):
    analyzer = analyze(
        model_builder("priority_inversion"),
        suppressions={("T.urgent.hiIn", "T.urgent.loIn")},
    )

    assert not findings_by_kind(analyzer, FindingKind.QUEUE_PRIORITY_INVERSION)


def test_shares_the_flow_engine_with_the_deadlock_analysis(model_builder):
    """The flow map narrows chains here exactly as it does for lock ordering"""
    analyzer = analyze(model_builder("priority_inversion"), flow=PortFlowMap.empty())

    assert analyzer.flow.is_empty
    # Conservative mode credits every handler with every output port, so both
    # async entries reach every downstream queue
    assert len(analyzer.hops) == 6
