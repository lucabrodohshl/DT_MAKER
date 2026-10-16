# Doppelganger

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/public/brand/doppelganger-dark.png">
  <img src="docs/public/brand/doppelganger-light.png" alt="Doppelganger logo: two mirrored profiles and an infinity loop" width="128" height="128">
</picture>

Doppelganger is a local platform for designing, verifying and operating digital twins. You describe a twin's structure and behavior in one design, inspect the generated formal models, verify a release, and run instances against a simulator or a plant connection. The runtime's semantic kernel decides state transitions and records them in a tamper-evident ledger.

It is intended for engineers exploring formal digital-twin workflows, developers extending the platform, and researchers who need to inspect the relationship between a model, its runtime behavior and its evidence.

![The indoor drone twin in Doppelganger](docs/screenshots/doppelganger-drone-hero.png)

*The bundled drone example shows the physical simulation beside the twin's known map. See the [operations guide](docs/studio/operations.md) for the meaning of the runtime views.*

## What you can do

- **Design a twin:** edit asset types, parts, properties, events, states and timed transitions in Structure and Behavior. The server generates a timed automaton, formal ontology, interpretation, RDF projections and data contract.
- **Verify a release:** validate the saved design, inspect alignment and release evidence, build a verified package and publish an immutable version.
- **Operate an instance:** deploy a published design, connect live data over supported REST or MQTT plant inputs, inspect telemetry and conformance, and replay recorded decisions.
- **Explore examples:** the repository includes a drone with an evolving map, an industrial pump, and a thermal chamber tutorial.

The browser presents data and results. Formal checks run in the backend, and the runtime kernel is the authority for behavioral state and admissible transitions. The [guarantees and limits](docs/limitations.md) explain the scope of these claims.

## Quick start

Install CMake 3.25+, a C++20 compiler, Node.js 20.19+ or 22.12+, npm, Python 3, curl, Git, flex, GNU bison 3.6+ and the Z3 development library. On macOS, `brew install z3 bison flex` supplies the last three. The first build fetches and compiles pinned UPPAAL libraries.

```bash
make demo
```

Open [http://127.0.0.1:8080](http://127.0.0.1:8080) when the command prints the URL. Choose **Running twins** to inspect the seeded drone and pump, or **Twin designs → New twin design** to start authoring. Press `Ctrl-C` in the terminal to stop the local stack. Demo data is stored in `var/demo/`; `make demo-fresh` resets that data deliberately.

For individual setup steps, see [installation](docs/installation.md) and [troubleshooting](docs/troubleshooting.md). The [first-twin tutorial](docs/studio/tutorial-first-twin.md) walks through a thermal chamber from a blank design.

## Documentation website

The [documentation](docs/index.md) is a separate VitePress static site. It can run with the application, backend and database stopped:

```bash
cd docs
npm ci
npm run dev
```

`npm run check` builds the site and validates links, pages, search assets and screenshots. `npm run preview` serves the production output from `docs/.vitepress/dist/`. The app bundles the same static site under **Help** at `/docs/`, but the documentation does not call the application. [Hosting instructions](docs/hosting.md) cover a standalone domain or static host.

Start with the [interface guide](docs/studio/getting-started.md), then choose a task:

| Task | Guide |
|---|---|
| Create and inspect a design | [Create your first twin](docs/studio/tutorial-first-twin.md) |
| Feed an instance from a real system | [Connect a running twin](docs/studio/tutorial-connect-real-twin.md) |
| Change a formal theory safely | [Evolve an ontology](docs/studio/tutorial-evolving-an-ontology.md) |
| Understand the implementation | [Architecture](docs/architecture.md), [canonical model](docs/model-architecture.md), [developer guide](docs/studio/developer.md) |
| Integrate with the runtime | [Runtime API](docs/runtime-api.md), [OpenAPI contracts](api/) |

## Build and test

```bash
cmake --preset studio-release
cmake --build build/studio-release -j 8
ctest --test-dir build/studio-release --output-on-failure

cd web/studio
npm ci
npm run typecheck
npm run lint
npm test
```

`make build` builds the C++ and web application; `make test-web` runs frontend checks; `make docs-site` checks the independent docs. Browser end-to-end tests use `cd web/studio && npx playwright test` against a running demo. The [contributor guide](CONTRIBUTING.md) has the development workflow.

## Repository map

| Path | Responsibility |
|---|---|
| `include/twin/`, `src/` | C++ libraries: model, formal tooling, kernel, runtime, platform and Studio service |
| `apps/` | CLI, service, runtime, simulator, feed and macOS launcher entry points |
| `web/studio/` | React and TypeScript interface and browser tests |
| `docs/` | Standalone static documentation source and product screenshots |
| `api/` | OpenAPI contracts |
| `examples/`, `models/` | Bundled examples and model inputs |
| `tests/` | C++ unit, integration, architecture and end-to-end suites |
| `SemPTDTAlignmentICSE/` | Existing semantic aligner, with its own license and research material |
| `proof/` | Handwritten correctness argument |

## Security and licensing

Studio currently has no authentication or authorization. The role selector only changes what the interface displays. Keep the service on a trusted local network; read [SECURITY.md](SECURITY.md) before reporting a vulnerability.

The root project is currently **unlicensed**. Its source is visible, but no permission to redistribute or modify it is granted by a root license. The bundled `SemPTDTAlignmentICSE/` code has its own MIT license. Licensing terms should be resolved before inviting outside contributions or publishing binary releases.
