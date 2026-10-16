# Publish a twin design and run it

The graphical editor creates a draft twin design. **Save** and **Validate** confirm its source is stored and compiled, but they do not publish it. The release area checks the exact version and packages its formal artefacts.

## Release → Package

From a design, choose **Publish** or **Advanced → Release → Package**. The release page lists each required check and a fix link for blocked items. Depending on the design, it checks structure and data, formal views and ontology, interpretation coverage, alignment, compilation, monitor configuration, scenarios and package integrity. The result is tied to the exact saved inputs; editing the draft can make previous evidence stale.

Resolve the listed blockers, then choose **Build package & bundle**. The verified core contains the compiled runtime model and pinned formal inputs. The deployment bundle contains the surrounding design, connectivity and simulation configuration. Inspect the package integrity result, then **Publish**. A published design version is immutable; edit it by creating another draft.

**Export** on this page produces the design bundle used by **New twin design → Import** on another Studio. The formal artefact lists have their own checkboxes and export dialog for .ont, .interp and timed automaton sources.

## Release → Instances

Choose **New instance** from the published design. Give the running twin an identifier and display name. Fill any per-twin property values and bind required context assets or data sources. One published design version can have several instances.

The instance pins the published version it was created from. A newer published version can be offered as an upgrade; it does not silently alter an existing running twin.

## Release → Deployment

Deploy the instance from its release page. Studio starts the runtime with the package for that version and, when configured, the world simulator or plant connection. The page reports processes, ports, current package and deployment state. Stop, start, upgrade or roll back from the instance's deployment controls.

Open **Running twins** to inspect live state, telemetry, conformance, monitors, prediction and audit history. If a runtime or source is not connected, Studio should show that state rather than inventing a verdict.

For a connected physical system, continue to [Connect a real twin](tutorial-connect-real-twin.md).
