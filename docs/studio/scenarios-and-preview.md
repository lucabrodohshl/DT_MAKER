# Test a twin design with scenarios and preview

Build the draft in Structure and Behavior first. The Generated automaton shows what the server compiled. To test how that automaton responds over time, open **Advanced → Test** for Scenario Builder or Preview.

## Scenario Builder

A scenario begins in the automaton's initial state at logical time zero. Add events, waits, telemetry, world changes and expectations in order. An event may be a positive step that should be accepted or a negative test that should be refused. Expectations can check reached state, transition, a property or a semantic formula.

When you choose an event, Studio asks the kernel for its legal time windows. It shows whether the event is available now, later, or unreachable by waiting from the current state. If you place it at an illegal time, inspect **Why?** and either move it to a legal time or keep it as an expected refusal.

Run one scenario while editing. **Run all tests** records evidence for the exact design inputs and contributes to release readiness. Scenario success exercises chosen paths; it does not prove every possible behavior.

## Preview

**Start preview** runs the design in an isolated session. Use it to send events, replay a scenario and inspect the runtime's current state and event log. Preview does not create a running twin instance or deployment. After the design is verified and published, use Release → Instances for a real running twin.

For a complete creation path, see [Create your first twin](tutorial-first-twin.md).
