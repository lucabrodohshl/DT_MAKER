# Release, instances and deployment

## Release → Package

**The release gate** lists every item a release depends on, computed from the section validators
and from evidence for exactly the version's inputs — the pinned artefact versions (and, for the
scenario tests, the Blueprint document):

| Item | Passes when |
|---|---|
| Structure, World, Data contract, Bindings | the section validates (warnings allowed); Structure also needs at least one asset |
| PT model, DT model, Ontology, PT / DT interpretation | the pinned version is **VALID** |
| Semantic alignment | the aligner's verdict is PASS (weak or strong) |
| Compiler | the DT view compiled with translation validation |
| Monitor validation | the monitors document is consistent, its data-quality fields are in the data contract, and every conformance label and temporal property is valid on the pinned views |
| Scenario regression (tests) | the scenario tests pass (not applicable without scenarios) |
| Package integrity | a Verified Core Package was built from exactly the pinned formal artefacts (its integrity checks passed when it was built), together with its Deployment Bundle, for the version's current content — any later edit of a draft detaches them and asks for a rebuild |

**READY TO RELEASE** needs every blocking item to pass; otherwise **RELEASE BLOCKED** lists the
blockers, each with a **Fix** link. **Build package & bundle** (**Rebuild** once built) succeeds
once every other blocking item passes; before that it is refused with the blockers.

![Release readiness](../screenshots/studio/studio-release-readiness.png)

The **Verified Core Package** (formally verified scope: package and IR hashes, model version,
integrity checks, the pinned formal inputs) and the **Deployment Bundle** (integrity-protected,
not formally verified: its files — the Blueprint document and the simulator inputs — with role,
hash and size, and the hash of each deployment section: structure, world, data, connectivity,
presentation, scenarios, simulation) are shown side by side. **Export** downloads the version as a
`twin-blueprint-bundle/1` document that another Studio can import.

![Verified Core Package and Deployment Bundle](../screenshots/studio/studio-package.png)

**Impact of this version** compares the version with its parent: which sections changed, whether
each change touches the verified core, the deployment bundle, tests or presentation only, and
what must be re-run (alignment, compilation, package) — a presentation-only change invalidates
no formal evidence.

**Publish** makes the version immutable, publishes the pinned formal drafts and marks the
package released. It is refused while the gate does not pass.

## Release → Instances

An instance is created from a **published** version:

- its **id** (by default also the id of its root asset) and display name;
- **identity** values (per-instance properties such as a serial number);
- **assets**: every instance-scoped asset is created (ids derived from the instance id unless
  given); every context asset is bound to an existing estate asset (created if missing);
- **placement** of the root asset in the estate hierarchy (when the root has no parent in the
  Blueprint);
- the **deployment target** (this Studio's supervisor).

Telemetry channels are created from the data contract, bound as the Blueprint's connectivity
says. An instance whose Blueprint has a newer published version shows **Upgrade**.

![Creating an instance](../screenshots/studio/studio-instance.png)

## Release → Deployment

**Deploy** starts the instance's runtime under the deployment supervisor of this Studio, on its
version's released package and bundle: `twin-runtime` (plus `twin-world` for co-simulation, or
`twin-pt-feed` for an event-script simulator), with a bridge that brings its telemetry into
Operate. The processes run in their own process group and are stopped cleanly (SIGTERM, then
SIGKILL after a grace period) when the instance is stopped or Studio exits; processes left behind
by a crash are found by their pid files and stopped before the instance starts again. While
Studio runs, a runtime that exits unexpectedly is shown as **FAILED** with the reason and is not
restarted automatically — a fail-stop (the ledger could not be written, for example) is an
integrity event an operator must see. When Studio starts, it redeploys every instance whose
desired state is *running* (including one that failed); an instance you stopped stays stopped
until you start or deploy it.

Each instance shows its deployed package, its processes, ports and state. **Stop** stops it;
**Start** starts it again on the package deployed for it; **Deploy vN** (an instance never
deployed), **Upgrade to vN** and **Deploy another version…** (upgrade or rollback) deploy the
package of that Blueprint version; **Open in Operate** opens the running twin. A package
deployed or rolled back through a change workspace (**Studio → Changes**, **Studio →
Deployments**) restarts a running instance on that package, so what is recorded as deployed is
always what executes.

![Deployment](../screenshots/studio/studio-deployment.png)

## In Operate

- **Open in Studio** (twin header) opens the Blueprint version the instance runs.
- **Behaviour → Monitors** shows the live result of every monitor, the alerts the alert policy
  raises now and the status of each requirement. Each monitor is evaluated by its authority:
  conformance by the runtime, property monitors on the kernel's committed state (with the model
  of exactly that version), data-quality monitors on the stored telemetry. A monitor that cannot
  be evaluated — the runtime is not running, a property uses semantic atoms, a range or quality
  check has no sample yet — is **UNKNOWN** with the reason, never a guessed verdict; a freshness
  or missing-data check with no sample reports a **FINDING**.

![Live monitors of an instance](../screenshots/first-twin/20-operate-monitors.png)
