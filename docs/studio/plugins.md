# Domain plugins (developer guide)

The generic product understands:
- assets, telemetry, events and relationships
- propositions, states, transitions and clocks
- predictions, ledgers and replay
- models, ontologies, interpretations, evidence, packages and deployments

It knows nothing about maps, batteries, floor plans or P&IDs. A **domain plugin** adds such a
view as an extra tab on the asset page of the twins it applies to. The product must work
unchanged without any plugin; the industrial pump has none.

## Rules

1. **A plugin displays, it never decides.** Modes, admissibility, enabled transitions,
   conformance and plan acceptance come from the kernel, through `state`, `runtime` and
   `subscribe`. A plugin must not contain logic such as "if battery < 20 then return".
2. **Live and replay never mix.** In `mode === 'replay'`, render only `replay` data, and label
   the view as a replay.
3. **Be honest without a runtime.** When `runtimeConnected` is false, render what you can
   from recorded data and say what is missing. Never show placeholder values that look real.
4. **Isolation.** Plugin code lives in `web/studio/src/plugins/<id>/`. Generic code never
   imports it: it asks the registry for plugins matching a twin.

## The contract

`web/studio/src/plugins/types.ts`:

```ts
interface DomainPlugin {
  id: string;                                   // stable id, e.g. "drone"
  title: string;                                // tab title, e.g. "Mission map"
  description: string;
  matches: (twin: TwinSummary) => boolean;      // usually twin.presentation.plugin === id
  Component: ComponentType<DomainPluginProps>;
}

interface DomainPluginProps {
  twin: TwinDetail;              // presentation metadata, deployment, bound artefacts, trust
  asset: AssetDetail | null;
  mode: 'live' | 'replay';
  state: RuntimeState | null;    // latest kernel state (live), refetched after gaps
  runtime: RuntimeApi;           // typed GET/POST to this twin's runtime/world/planner via the proxy
  subscribe(topics, onEvent, onGap?): () => void;   // ordered runtime stream events
  replay: ReplayFrame | null;    // current replay frame (replay mode)
  runtimeConnected: boolean;
}
```

The product specification's lifecycle hooks map onto these props:

| Hook | In this API |
|---|---|
| `onAssetLoaded` | `twin` / `asset` props change |
| `onSemanticState` | `state` changes (already ordered and resynchronised) |
| `onTelemetry`, `onTransition`, `onPrediction`, planner updates | `subscribe(['telemetry' \| 'decision' \| 'planning' \| 'plan' \| 'map' \| …], cb, onGap)` |
| `onReplayFrame` | `mode === 'replay'` and `replay` changes |

Event payloads are documented in [`../runtime-api.md`](../runtime-api.md#live-stream) and
[`../../api/runtime.openapi.yaml`](../../api/runtime.openapi.yaml). Replay frames carry the
recomputed ledger record body. Use the accessors in `@/runtime/replay` (`frameTicks`,
`frameTransition`, `frameLabel`, `stateAt`, `propositionsAt`) rather than reading fields ad
hoc.

When `onGap` fires, events were lost or the runtime restarted. Refetch whatever you display
from the runtime API; do not try to patch it up.

## Registering a plugin

```ts
// web/studio/src/plugins/conveyor/index.ts
import { registerPlugin } from '@/plugins/registry';
import { ConveyorView } from './ConveyorView';

registerPlugin({
  id: 'conveyor',
  title: 'Line diagram',
  description: 'Production-line diagram of a conveyor section.',
  matches: (twin) => twin.presentation?.plugin === 'conveyor',
  Component: ConveyorView,
});
```

```ts
// web/studio/src/plugins/index.ts
import './drone';
import './conveyor';
```

A twin opts in through its **presentation metadata** (`twin.presentation` in the example's
`example.json`, next to `timeUnit` and the per-state labels):

```json
"presentation": {
  "plugin": "conveyor",
  "timeUnit": "s",
  "states": { "RUNNING": { "label": "Running", "tone": "ok", "summary": "Belt moving at set speed." } }
}
```

Presentation metadata is consumed only by the UI. The runtime ignores it, and changing it never
invalidates evidence.

## The reference plugin: drone

`web/studio/src/plugins/drone/` draws:
- the physical world and the twin-known world
- the drone, its executed path and its current and invalidated plans
- discovered obstacles and targets
- planner candidates with the kernel's verdicts
- a mission timeline built from ledger records

In replay it reconstructs all of this from the replay frames, context records and recorded
telemetry, and it never shows physical ground truth as twin knowledge. Removing
`import './drone'` removes the tab and nothing else. Every generic view of the drone keeps
working.

## Testing

- Unit-test pure helpers with Vitest (`npm test`).
- Add Playwright scenarios under `web/studio/e2e/` that run against the real stack
  (`npm run e2e`). `e2e/drone-integration.spec.ts` is the reference.
