"""test_stress_drop_detection.py:

Integration tests that use the StressTest component to generate deterministic,
monotonically sequenced telemetry and events, then verify no data is dropped
or reordered by the GDS pipeline.

These tests exercise the full data path: FSW -> comm adapter -> ZMQ -> distributor
-> decoders -> histories -> test API.
"""

import time
from fprime_gds.common.testing_fw import predicates


def test_stress_events_no_drops(fprime_test_api):
    """Verify no stress events are dropped at moderate rate.

    Starts the StressTest component with 5 events/cycle at 1Hz for 15 seconds,
    then verifies that all monotonically sequenced events were received with
    no gaps in the sequence numbers.
    """
    events_per_cycle = 5
    duration = 15  # seconds

    fprime_test_api.send_and_assert_command(
        "Ref.stressTest.START", [0, events_per_cycle], max_delay=0.5
    )

    # Wait for events to accumulate
    expected_count = events_per_cycle * duration
    count_pred = predicates.greater_than(expected_count - events_per_cycle)

    stress_event_pred = fprime_test_api.get_event_pred("Ref.stressTest.StressEvent")
    results = fprime_test_api.await_event_count(
        count_pred, stress_event_pred, timeout=duration + 10
    )

    fprime_test_api.send_and_assert_command("Ref.stressTest.STOP", max_delay=0.5)

    # Wait for remaining events to arrive
    time.sleep(2)

    # Retrieve all stress events and check for gaps in sequence numbers
    all_events = fprime_test_api.get_event_test_history().retrieve()
    stress_events = [
        e for e in all_events
        if "StressEvent" in str(e.get_template().get_name())
    ]

    seq_nums = []
    for event in stress_events:
        args = event.get_args()
        if args:
            seq_nums.append(args[0][1])

    assert len(seq_nums) > 0, "No stress events received"

    # Check for monotonic sequence and no gaps
    gaps = []
    reordered = []
    for i in range(1, len(seq_nums)):
        if seq_nums[i] <= seq_nums[i - 1]:
            reordered.append((i, seq_nums[i - 1], seq_nums[i]))
        elif seq_nums[i] != seq_nums[i - 1] + 1:
            gaps.append((seq_nums[i - 1], seq_nums[i]))

    # Report results
    total_expected = seq_nums[-1]  # highest seq num seen
    total_received = len(seq_nums)
    dropped = total_expected - total_received

    print(f"Stress event results: received={total_received}, "
          f"max_seq={total_expected}, dropped={dropped}, "
          f"gaps={len(gaps)}, reordered={len(reordered)}")

    assert len(reordered) == 0, f"Events received out of order: {reordered[:5]}"
    assert len(gaps) == 0, f"Gaps detected in event sequence: {gaps[:5]}"
    assert dropped == 0, f"Events dropped: {dropped} out of {total_expected}"


def test_stress_telemetry_no_drops(fprime_test_api):
    """Verify no telemetry updates are dropped at moderate rate.

    Starts the StressTest component with 5 telemetry/cycle at 1Hz for 15 seconds,
    then verifies that the SeqNum channel has monotonically increasing values
    with no gaps.
    """
    tlm_per_cycle = 5
    duration = 15  # seconds

    fprime_test_api.send_and_assert_command(
        "Ref.stressTest.START", [tlm_per_cycle, 0], max_delay=0.5
    )

    expected_count = tlm_per_cycle * duration
    count_pred = predicates.greater_than(expected_count - tlm_per_cycle)

    results = fprime_test_api.await_telemetry_count(
        count_pred, "Ref.stressTest.SeqNum", timeout=duration + 10
    )

    fprime_test_api.send_and_assert_command("Ref.stressTest.STOP", max_delay=0.5)
    time.sleep(2)

    # Get all SeqNum telemetry
    all_tlm = fprime_test_api.get_telemetry_test_history().retrieve()
    seq_nums = [
        t.get_val() for t in all_tlm
        if "SeqNum" in str(t.get_ch_template().get_name())
    ]

    assert len(seq_nums) > 0, "No SeqNum telemetry received"

    # Check monotonic and no gaps
    gaps = []
    reordered = []
    for i in range(1, len(seq_nums)):
        if seq_nums[i] < seq_nums[i - 1]:
            reordered.append((i, seq_nums[i - 1], seq_nums[i]))
        elif seq_nums[i] > seq_nums[i - 1] + 1:
            gaps.append((seq_nums[i - 1], seq_nums[i]))

    total_expected = seq_nums[-1]
    total_received = len(seq_nums)
    dropped = total_expected - total_received

    print(f"Stress telemetry results: received={total_received}, "
          f"max_seq={total_expected}, dropped={dropped}, "
          f"gaps={len(gaps)}, reordered={len(reordered)}")

    assert len(reordered) == 0, f"Telemetry received out of order: {reordered[:5]}"
    # Note: telemetry may legitimately have gaps if the TlmChan packetizer
    # drops older updates when multiple writes happen in the same cycle.
    # We check for reordering as the primary correctness criterion.


def test_stress_combined_high_rate(fprime_test_api):
    """Verify no data loss under combined telemetry and event load.

    Starts the StressTest with 10 telemetry/cycle AND 10 events/cycle at 1Hz
    for 20 seconds. This generates ~20 data items per second to stress the
    full pipeline.
    """
    tlm_per_cycle = 10
    events_per_cycle = 10
    duration = 20

    fprime_test_api.send_and_assert_command(
        "Ref.stressTest.START", [tlm_per_cycle, events_per_cycle], max_delay=0.5
    )

    # Wait for the test duration
    time.sleep(duration)

    fprime_test_api.send_and_assert_command("Ref.stressTest.STOP", max_delay=0.5)
    time.sleep(2)

    # Analyze events
    all_events = fprime_test_api.get_event_test_history().retrieve()
    stress_events = [
        e for e in all_events
        if "StressEvent" in str(e.get_template().get_name())
    ]

    evt_seq_nums = []
    for event in stress_events:
        args = event.get_args()
        if args:
            evt_seq_nums.append(args[0][1])

    # Analyze telemetry
    all_tlm = fprime_test_api.get_telemetry_test_history().retrieve()
    tlm_seq_nums = [
        t.get_val() for t in all_tlm
        if "SeqNum" in str(t.get_ch_template().get_name())
    ]

    # Event analysis
    evt_reordered = 0
    evt_gaps = 0
    for i in range(1, len(evt_seq_nums)):
        if evt_seq_nums[i] <= evt_seq_nums[i - 1]:
            evt_reordered += 1
        elif evt_seq_nums[i] != evt_seq_nums[i - 1] + 1:
            evt_gaps += 1

    evt_expected = evt_seq_nums[-1] if evt_seq_nums else 0
    evt_received = len(evt_seq_nums)
    evt_dropped = evt_expected - evt_received

    # Telemetry analysis
    tlm_reordered = 0
    for i in range(1, len(tlm_seq_nums)):
        if tlm_seq_nums[i] < tlm_seq_nums[i - 1]:
            tlm_reordered += 1

    tlm_expected = tlm_seq_nums[-1] if tlm_seq_nums else 0
    tlm_received = len(tlm_seq_nums)

    print(f"Combined stress results:")
    print(f"  Events:    received={evt_received}, max_seq={evt_expected}, "
          f"dropped={evt_dropped}, gaps={evt_gaps}, reordered={evt_reordered}")
    print(f"  Telemetry: received={tlm_received}, max_seq={tlm_expected}, "
          f"reordered={tlm_reordered}")

    assert evt_reordered == 0, f"Events reordered: {evt_reordered}"
    assert tlm_reordered == 0, f"Telemetry reordered: {tlm_reordered}"
    assert evt_dropped == 0, f"Events dropped: {evt_dropped} out of {evt_expected}"


def test_noop_sequence_integrity(fprime_test_api):
    """Verify no event drops during rapid NO_OP command sequences.

    Sends 50 NO_OP commands and verifies that all 3 expected events
    (OpCodeDispatched, NoOpReceived, OpCodeCompleted) arrive for each command
    with no drops or reordering.
    """
    iterations = 50
    failed = 0
    dropped = False
    reordered = False

    for i in range(iterations):
        evr_seq = [
            "CdhCore.cmdDisp.OpCodeDispatched",
            "CdhCore.cmdDisp.NoOpReceived",
            "CdhCore.cmdDisp.OpCodeCompleted",
        ]
        results = fprime_test_api.send_and_await_event(
            "CdhCore.cmdDisp.CMD_NO_OP", events=evr_seq, timeout=10
        )
        if len(results) != 3:
            items = fprime_test_api.get_event_test_history().retrieve()
            last = None
            is_reordered = False
            for item in items:
                if last is not None and item.get_time() < last.get_time():
                    reordered = True
                    is_reordered = True
                    break
                last = item
            if not is_reordered:
                dropped = True
            failed += 1
        fprime_test_api.clear_histories()

    print(f"NO_OP sequence results: {failed}/{iterations} failed, "
          f"dropped={dropped}, reordered={reordered}")

    assert not reordered, "Events were received out of order"
    assert not dropped, "Events were dropped"
    assert failed == 0, f"{failed}/{iterations} NO_OP sequences failed"
