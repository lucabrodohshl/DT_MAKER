# Contributing and local development

Doppelganger contains a C++20 engine and services, a React/TypeScript frontend, and this independent documentation site. Start with the [architecture](architecture.md) and [developer guide](developer.md) before changing behavior.

## Set up a working checkout

1. Install the tools in [installation](installation.md).
2. Run `cmake --preset studio-release` and `cmake --build build/studio-release -j 8` for the C++ targets.
3. Run `npm ci` in `web/studio` for frontend dependencies.
4. Run `npm ci` in `docs` for documentation dependencies. The packages have separate lockfiles.

Keep generated build outputs, runtime data and editor caches out of commits. The `.gitignore` covers `build/`, `var/`, `dist/`, `node_modules/` and VitePress output.

## Checks before a change is shared

```bash
ctest --test-dir build/studio-release --output-on-failure
cd web/studio && npm run typecheck && npm run lint && npm test
cd ../../docs && npm run check
```

For a change to a visible workflow, run `make demo` and verify it in the browser. The browser end-to-end suite runs with `cd web/studio && npx playwright test` against a running demo.

## Documentation changes

Write task-oriented guides using labels that appear in the current interface. Include a result the reader can verify and a screenshot when visual orientation helps. Screenshot capture scripts are under `scripts/screenshots/`; review images for sensitive values before committing them. Run `npm run check` in `docs` to catch dead links and missing static assets. Site layout and navigation live in `docs/.vitepress/`.

## Issues and security reports

Use GitHub issues for reproducible defects and feature discussions. Include the command, expected result, actual result and relevant sanitized log lines. Follow [SECURITY.md](https://github.com/lucabrodohshl/DT_MAKER/blob/main/SECURITY.md) for a security issue instead of posting exploit details publicly.

The root project currently has no license. Until one is chosen, do not assume permission to redistribute modified versions. The bundled `SemPTDTAlignmentICSE` component retains its own license and attribution.
