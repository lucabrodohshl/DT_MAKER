# Troubleshooting

## The demo does not start

1. Check that the tools in [installation](installation.md#requirements) are available. `cmake --version`, `node --version`, `npm --version`, `python3 --version` and `curl --version` should all run.
2. Check whether port 8080 is occupied. The script stops with an explicit message in that case. Set a different port for this launch, for example `STUDIO_PORT=8081 make demo`.
3. Read `var/demo/logs/twin-studio.log` and the instance logs beneath `var/demo/studio/instances/`. The demo prints those paths when it starts.
4. If the C++ build fails while finding Z3 or UPPAAL libraries, install the Z3 development package and rerun `./scripts/bootstrap-deps.sh` before `make demo`.

`make demo-fresh` removes `var/demo/` and reseeds the examples. Use it only when you intend to discard local demo data.

## The page loads but a twin is unavailable

Open the instance's **Deployment** view and inspect the runtime state. Studio supervises one runtime per deployed instance. A missing runtime is shown as unavailable; it is not a passing conformance result. Check the instance log directory reported in `var/demo/logs/twin-studio.log` before resetting data.

## An edit does not compile

Wait for **Saved** in the design editor, then open **Problems** or choose **Validate**. Select a diagnostic to return to its source element. Common causes are a missing event trigger, a formula that names an unknown symbol, or an invalid time bound. [Compiler diagnostics](compiler-diagnostics.md) documents supported model forms and the compiler's refusal messages.

## A release is blocked

Open **Publish** on the design and inspect each release item. The gate is tied to the exact saved inputs. A later edit can make previous verification evidence stale; rerun the indicated checks. The [release guide](blueprint-release.md) explains the sequence.

## A plant message is rejected

Open the instance's **Plant connection** page and compare the configured plant names, event names, timestamp field and unit with the message. Then inspect **Behavior** and **Audit** for the kernel's refusal reason. Events outside an enabled transition or permitted time window are rejected. See [connect a real system](tutorial-connect-real-twin.md).

## The documentation site does not build

Run `npm ci` in `docs/`, then `npm run check`. VitePress reports dead links with source filenames. Set `DOCS_BASE` to a path beginning and ending with `/`, such as `/project/`, only when the site will be hosted below that path. The site requires Node.js; it does not require Studio, SQLite, Z3 or CMake.
