# Install and start Doppelganger

Doppelganger has a C++ engine and service, a React browser interface, and a local SQLite database. The demo command builds the components, seeds the bundled pump and drone examples, and starts the service. You do not need Docker.

## Requirements

| Tool | Why it is needed |
|---|---|
| CMake 3.25 or newer | Configure and build the C++ targets |
| C++20 compiler | Build the engine, runtime, simulator and Studio service |
| Node.js 20.19 or newer, or 22.12 or newer, and npm | Build the React interface and independent docs site |
| Python 3, Git and curl | Bootstrap the pinned UPPAAL libraries and check service readiness |
| Flex and GNU Bison 3.6 or newer | Build the pinned UPPAAL parser |
| Z3 development library | Ontology and interpretation checks |

The [bootstrap script](https://github.com/lucabrodohshl/DT_MAKER/blob/main/scripts/bootstrap-deps.sh) builds the pinned third-party UPPAAL libraries on first use. On macOS, install Z3 with `brew install z3`. On Linux, install your distribution's Z3 development package and the other tools above.

## Start the bundled demo

From the repository root:

```bash
make demo
```

The first run can take several minutes because it builds third-party libraries and the application. When the terminal prints the Studio URL, open `http://127.0.0.1:8080`. Keep the terminal running; press `Ctrl-C` to stop all processes it started.

The demo persists its SQLite database, logs and example state under `var/demo/`. Subsequent starts reuse that data. `make demo-fresh` deliberately deletes that demo data and seeds the examples again.

::: tip Check the first launch
Open **Running twins** and select the indoor inspection drone or process pump. The status should become **Live** after the deployment supervisor starts their runtimes. If it does not, see [troubleshooting](troubleshooting.md).
:::

![Running twins overview on a seeded first launch](./screenshots/current/running-twins.png)

<p class="screenshot-caption">On first launch, the demo seeds one process pump and one indoor drone.</p>

## Build components separately

```bash
cmake --preset studio-release
cmake --build build/studio-release -j 8
cd web/studio
npm ci
npm run build
```

Use `./scripts/start-demo.sh --no-build` from the repository root to start from already built binaries and frontend assets. For frontend development, keep Studio running and run `npm run dev` in `web/studio`; Vite proxies `/api` to `http://127.0.0.1:8080` by default.

## Next step

Read [first launch](getting-started.md) for the interface map, then follow [create your first twin](tutorial-first-twin.md). The documentation site itself can be started separately with the commands in [Docs hosting](hosting.md); it does not need the application.
