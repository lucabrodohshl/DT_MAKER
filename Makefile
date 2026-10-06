# Convenience entry points. The build itself is CMake (see CMakePresets.json).
#   make demo      one command: build what is missing, seed, start everything, print the URL
#   make build     C++ (release preset, with Verified Twin Studio) + web UI
#   make test      all C++ test suites (unit, e2e) + Studio suites
#   make docs      Doxygen API documentation (make proof: the LaTeX proof)
#   make macos-app dist/macos/Verified Twin Studio.app + .dmg (self-contained, ad-hoc signed)
#   make screenshots  regenerate docs/screenshots from the running product
#   make test-web  web UI: typecheck, lint, unit/component tests
#   make e2e       Playwright end-to-end tests against the running demo (make demo first)
#   make docs-site documentation website (HTML) -> web/studio/dist/docs, served at /docs/
#   make api-check OpenAPI: generated schemas up to date, client runtime types vs the runtime
#                  contract, and live twin-studio responses vs the Studio contract (needs the demo)
.PHONY: demo demo-fresh build test docs proof clean-demo macos-app screenshots test-web e2e api-check docs-site

PRESET ?= studio-release

demo:
	./scripts/start-demo.sh

demo-fresh:
	./scripts/start-demo.sh --fresh

build:
	cmake --preset $(PRESET)
	cmake --build build/$(PRESET) -j 8
	cd web/studio && npm ci --no-audit --no-fund && npm run build

test:
	cmake --build build/$(PRESET) -j 8
	ctest --test-dir build/$(PRESET) --output-on-failure

docs:
	cmake --build build/$(PRESET) --target docs

proof:
	$(MAKE) -C proof

clean-demo:
	rm -rf var/demo

macos-app:
	./scripts/macos/build-app.sh

screenshots:
	./scripts/capture-screenshots.sh

test-web:
	cd web/studio && npx tsc -b && npx eslint . && npx vitest run

e2e:
	cd web/studio && npx playwright test

api-check:
	python3 scripts/openapi/studio_components.py --check
	python3 scripts/openapi/check_runtime_types.py
	python3 scripts/openapi/check_studio_contract.py
	python3 scripts/openapi/check_engine_contract.py build/$(PRESET)

docs-site:
	python3 scripts/docs/build_site.py
