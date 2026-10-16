# Changes, releases and deployment

The normal path is **Twin designs → open draft → Validate → Publish**. The graphical editor stores the source design; the server compiles its behavior, formal ontology, interpretations and contract. The release area verifies the exact saved version before it can create running twins.

## Change workspace

For an already deployed twin, open **Studio → Changes** to start a tracked engineering change. Select the affected twin and artefact versions, make an editable draft and record why it is changing. The change workspace keeps the candidate and the deployed baseline visible together.

## Release pipeline

Open **Release → Package** from the design. Work through the gate's listed validation, alignment, compilation, monitor and scenario items. Build the verified core and deployment bundle, inspect package integrity and publish the version. A later edit can invalidate evidence; rerun the affected steps. Published versions are immutable.

## Deployment and rollback

Under **Release → Instances**, create a running twin from a published design. Under **Release → Deployment**, deploy, stop, start, upgrade or roll back the instance. The page shows the exact package and current process state. A rollback selects an earlier released package; it does not edit that package.

For a physical plant, configure its REST or MQTT connection on the instance and use the connection page's actual address, token and message format. See [Connect a real twin](tutorial-connect-real-twin.md).

## Audit and logs

**Engineering audit** records who changed, validated, released or deployed an artefact. The running twin's execution ledger records its operational decisions. Review both when diagnosing a changed result. [Audit and replay](audit-and-replay.md) explains the difference.
