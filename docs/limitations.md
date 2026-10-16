# Guarantees and limits

Doppelganger composes a semantic aligner, a translation-validated compiler and a small execution kernel. The detailed [architecture](architecture.md), [trusted computing base](trusted-computing-base.md), [logical time model](logical-time-model.md) and [supported model fragment](supported-model-fragment.md) define the scope of that claim.

- **Logical time:** the formal relation is in logical time. The system does not claim hard real-time equivalence with the physical plant.
- **Proof status:** the argument in `proof/` is handwritten, not mechanized. Tests exercise the implementation but do not prove it.
- **Ledger:** hash chaining makes changes detectable. It does not make storage immutable. External anchoring is a separate operation.
- **Physical side:** the bundled drone is simulated and the bundled pump feed is scripted. Neither represents aerodynamic or industrial plant certification.
- **Authentication:** Studio currently has no authentication or authorization. The role selector changes presentation depth; it is not a security boundary. Do not expose the service to untrusted networks as-is.
- **Model coverage:** the compiler intentionally rejects features outside its verified fragment, including data variables and broadcast channels.
- **Formal check latency:** alignment and refinement checks run synchronously in the request that triggers them, so large models can make a request slow.
- **Planning estimates:** a candidate route is tested with estimated arrival times. The kernel verdict is exact for that schedule, while the timing estimate remains a modeling assumption.

For safety-relevant deployment, evaluate the full trusted computing base, the plant adapter and the operational environment separately. The documentation's walkthroughs show product behavior, not a certification claim.
