# Use a running twin

A **running twin** is a concrete instance of a published twin design. Open **Running twins** in the top bar and choose an instance. Its views use the exact package and formal artefacts pinned by its deployed design version.

## Assets and the knowledge graph

The asset view shows this instance and its surrounding assets, properties and relationships. It is a graph of concrete facts. The formal ontology used to interpret behavior is a separate, richer theory; open the design's **Generated → Formal ontology** to inspect it.

## Telemetry and meaning

Telemetry shows the values received from a simulator or real system, with time and quality. Interpretations connect behavior labels to formulas over the ontology. A semantic value is shown only when its evaluation has enough evidence; unknown remains unknown.

## Behaviour and conformance

The runtime kernel selects the current state and decides which transitions are enabled. The Behavior view shows that state, the possible next events and the recent path. An event outside the model or time window is refused and recorded as a conformance deviation. The browser displays the kernel's decision.

## Monitors, prediction and history

Monitors and alerts show current findings and reasons. Prediction and what-if ask the kernel about possible future behavior; they do not change the running twin. Audit and replay let you inspect how the instance reached its state and which package it used.

If the runtime is disconnected, check Deployment on the instance. An unavailable runtime is not a passing conformance check. See [Connect a real twin](tutorial-connect-real-twin.md) for the plant input workflow.
