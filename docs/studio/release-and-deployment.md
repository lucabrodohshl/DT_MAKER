# Releases, deployment and rollback

## Change workspace

A **change** coordinates the artefact drafts that must evolve together for one twin, for
example ontology `process-pump@3` together with interpretation `p101-dt-semantics@2`. Under
**Maintenance → Change workspace** you can:
- open a change for a twin, with a title and description
- **Add artefact**, which creates a draft from the version the twin currently deploys (you can
  also attach a draft when creating it from a version page)
- review each artefact's structural diff, the interpretation entries it affects, and links to
  its source diff and impact
- follow the **release pipeline**, then **Release** or **Abandon** (abandoning requires a
  reason)

## Release pipeline

The pipeline is computed on every view from stored evidence for **the exact candidate
artefacts**. It is never a checklist that someone ticks.

| Stage | Mandatory | Evidence |
|---|---|---|
| Edit & save drafts | yes | the draft versions in the change |
| Validate | yes | validation evidence for each draft |
| Check refinement (Def. 4) | advisory | refinement evidence (it enables the Theorem 3 route) |
| Analyze impact | advisory | computed on demand |
| Check / re-check alignment | yes | preserved via Theorem 3 (citing both evidence records), or new alignment evidence |
| Compile DT view | yes | Twin IR hash and compilation evidence (translation validation) |
| Build verified package | yes | the package and its integrity checks |
| Verify package | yes | integrity verification of the stored package |
| Release | yes | publishes the versions and marks the package released |
| Deploy | advisory | an explicit deployment record |

Each stage shows its state (**Pass**, **Fail**, **Unknown**, **Not checked**, **Stale**,
**Blocked**), the evidence links, the time and any diagnostics. Runnable stages have
**Run / Re-run**. **Release is blocked** while any mandatory stage is not Pass; the server
enforces this and lists the blocking stages. Release publishes every version in the change
(each must be VERIFIED) and closes the change.

![Release pipeline](../screenshots/tutorial/08-pipeline.png)

## Verified packages

A package (**Engineering → Packages**) binds immutable artefact versions together:
- PT view, DT view, ontology, PT and DT interpretations, each with its version and hash
- the Twin IR hash
- the alignment evidence
- the compiler and kernel versions
- the package hash

**Verify** re-checks its integrity: every content hash, the manifest and the package hash.
The result lists every check and the first failure, if any. A package is never modified after
it is built; a correction is a new package.

## Deployment

**Engineering → Deployments** is the append-only history per twin. Each entry records:
- the package and the previous package
- the kind (deploy / rollback)
- the reason, actor and time

Deploying verifies the package's integrity first and refuses a package that fails. A twin
always points at exactly one package: the runtime executes that package, and its ledger
records the package hash.

![Deployments](../screenshots/tutorial/09-deployments.png)

## Rollback

**Maintenance → Rollback** redeploys a previously released package. Before you confirm, it
shows:
- the current deployment and the rollback target
- per role (model, ontology, interpretations), what differs between them
- the target's evidence and its **package integrity**, verified at that moment

A **reason is required**. The rollback is a new deployment record of kind *rollback*, and it
appears in the engineering audit. **Nothing newer is deleted**: newer versions, packages and
evidence remain, and you can deploy them again later.

## Engineering audit trail

**Audit → Engineering audit** records every artefact and lifecycle operation, separately from
the execution ledger:

| Area | Operations |
|---|---|
| Artefacts | `artifact.create`, `artifact.draft.create`, `artifact.draft.save`, `artifact.validate`, `artifact.publish`, `artifact.reject` |
| Checks | `refinement.run`, `alignment.run`, `model.compile` |
| Packages | `package.build`, `package.verify`, `package.release` |
| Changes | `change.create`, `change.release`, `change.abandon` |
| Deployments | `deployment.deploy`, `deployment.rollback` |

Each record holds:
- time and actor
- the artefact ids, versions and hashes
- the operation and outcome
- the associated evidence and the reason, if any

Records are hash-chained; **Verify** recomputes the chain and reports the first invalid record.

The actor is the name chosen in the top-bar actor menu (sent as `X-Twin-Actor`). It identifies
who acted; it is **not** authentication. See the [security notes](developer.md#security-notes).

## System logs

**Administration → System logs** is the diagnostic log of `twin-studio`. You can filter by:
- time and level
- component
- correlation (request) id
- execution and asset

Logs are diagnostics. They are neither evidence nor an audit trail, and they never contain
secrets.
