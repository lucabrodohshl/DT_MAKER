# Getting started

## Start the demo

```bash
./scripts/start-demo.sh          # or: make demo
```

This command builds whatever is missing and seeds both examples on the first start. It then
starts every process and prints the URL (http://127.0.0.1:8080):

| Process | Port | Role |
|---|---|---|
| `twin-studio` | 8080 | Web UI, platform API (SQLite + content-addressed object store), runtime proxy |
| `twin-runtime` | 8090 | Indoor inspection drone twin (co-simulation; waits for *Start mission*) |
| `twin-world` | 8091 | Drone physical-twin simulator and building-information service |
| `twin-runtime` | 8092 | Industrial pump P-101 twin (monitor mode) |
| `twin-pt-feed` | — | Scripted PLC feed of pump P-101 |

The script accepts these options:
- `--fresh` deletes the demo data first.
- `--no-build` uses the existing binaries.
- `--data DIR` chooses another data directory (default `var/demo`).

You can override the ports with `STUDIO_PORT`, `DRONE_PORT`, `WORLD_PORT` and `PUMP_PORT`.

The seed provides history from the start: a week of pump telemetry, recorded executions,
ontology versions (one published, one rejected, non-refining relaxation), validation, refinement
and alignment evidence, packages and deployments. Replay, the ledger and the engineering views
are therefore usable before you run anything.

Everything is persisted in the data directory and survives restarts. The browser keeps only
viewer conveniences: theme, Operations/Engineering mode and the actor name.

## Layout

- **Left navigation**:
  - Overview
  - Assets (explorer, knowledge graph)
  - Operations (live monitoring, telemetry, events)
  - Behaviour & prediction
  - Audit (timeline, provenance, ledger, engineering audit)
  - Engineering (assurance, ontologies, interpretations, models, verification, packages,
    deployments)
  - Maintenance (change workspace, impact, history, rollback)
  - Administration (system logs)
  - **Digital twins**: one entry per registered twin. It opens the twin's domain view when a
    plugin applies to it, otherwise the generic asset page.
- **Top bar**:
  - global search (⌘K / Ctrl-K, or `/`)
  - live connection state, with a button to pause and resume live updates
  - the **Operations / Engineering** switch
  - the actor menu
- **Breadcrumbs** on every page show the asset, twin, execution or version in context. Every
  entity has a URL, so links can be shared:
  - `/assets/pump-p101/telemetry`
  - `/engineering/ontologies/process-pump/versions/1`
  - `/maintenance/refinement/EV-0016`
  - `/audit/executions/pump-p101-dt/<session>/replay`

## Operations and Engineering modes

The two modes show the same product at different depths (progressive disclosure):
- **Operations** shows status, modes, telemetry, alerts, predictions and plain-language
  explanations.
- **Engineering** adds guards, clocks, formulas, hashes, evidence identifiers and checker
  identities.

Nothing is hidden for security reasons. Access control belongs to the server; the switch only
reduces visual noise.

## Personas

| You are | Start at |
|---|---|
| Operator | Overview → the asset → *Overview* and *Behaviour* tabs; Events & alerts |
| Reliability engineer | Telemetry (history, export), Behaviour → Conformance, Audit → Timeline, Replay |
| Digital-twin / formal engineer | Behaviour → graph, Engineering → Models / Verification / Packages |
| Ontology engineer | Engineering → Ontologies / Interpretations, Maintenance |
| Auditor | Audit → Execution ledger (verify), Decision provenance, Engineering audit |
| Administrator | Engineering → Deployments, Maintenance → Rollback, Administration → System logs |

## Search

Global search covers:
- assets and twins
- ontology symbols, such as `bearing_temp_limit`
- artefact versions, such as `process-pump@1`
- interpretation entries, evidence identifiers, packages, deployments and changes

Search does not index raw ledger payloads. To search those, use the ledger page's filters.

## Live data

Live updates arrive over Server-Sent Events. Every event carries a sequence number:
- When an event is missed or the server restarts (detected through a new epoch), Studio
  refetches the authoritative state. It never guesses.
- Updates never reorder a table you are reading, move the focus, reset a selection, or touch a
  draft you are editing.
- **Pause** stops applying updates and counts the ones that arrive. **Resume** refetches
  everything at once.

When data is old, Studio says so: *Data is stale: last update 23 minutes ago*. It never shows
stale data as live.

## Time

Studio distinguishes four kinds of time and labels each of them:

| Time | Where | Meaning |
|---|---|---|
| **Logical model time** | behaviour, prediction, ledger, replay | The kernel's exact integer ticks, shown as decimal text (never floating point) |
| **Observation time** | telemetry | When the source observed the value |
| **Ingestion time** | telemetry detail, CSV export | When Studio received it |
| **Evidence time** | engineering | When a check ran, a version was published or a package deployed |

Wall-clock times are shown in your browser's time zone. The zone name appears in the tooltip,
and the ISO-8601 UTC value is used in exports.
