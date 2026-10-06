#!/usr/bin/env python3
"""Regenerates components.schemas of api/studio.openapi.yaml from web/studio/src/api/types.ts.

Everything above the "generated" marker line is hand-written (paths, parameters, responses,
wrapper schemas); everything below it is produced here from the client types through
web/studio/scripts/ts-to-jsonschema.mjs, so the contract and the typed client cannot drift
apart silently. Whether the *server* honours the contract is checked separately against a
running instance by check_studio_contract.py.

  python3 scripts/openapi/studio_components.py           # rewrite
  python3 scripts/openapi/studio_components.py --check   # exit 1 if the file is out of date
"""
import json
import pathlib
import subprocess
import sys

import yaml

ROOT = pathlib.Path(__file__).resolve().parents[2]
SPEC = ROOT / "api" / "studio.openapi.yaml"
WEB = ROOT / "web" / "studio"
MARKER = "    # --- generated from web/studio/src/api/types.ts"


def generated_block() -> str:
    out = subprocess.run(
        ["node", "scripts/ts-to-jsonschema.mjs", "src/api/types.ts"],
        cwd=WEB, check=True, capture_output=True, text=True,
    ).stdout
    schemas = json.loads(out)
    text = yaml.safe_dump(schemas, sort_keys=True, allow_unicode=True, width=110)
    return "".join(f"    {line}\n" if line else "\n" for line in text.splitlines())


def main() -> int:
    current = SPEC.read_text(encoding="utf-8")
    head, sep, _ = current.partition(MARKER)
    if not sep:
        print(f"marker not found in {SPEC}", file=sys.stderr)
        return 2
    marker_line = MARKER + current.partition(MARKER)[2].split("\n", 1)[0] + "\n"
    updated = head + marker_line + generated_block()
    spec = yaml.safe_load(updated)
    hand = {k for k in spec["components"]["schemas"]}
    missing = sorted({r.split("/")[-1] for r in refs(spec)} - hand)
    if missing:
        print("unresolved $refs: " + ", ".join(missing), file=sys.stderr)
        return 1
    if "--check" in sys.argv:
        if updated != current:
            print(f"{SPEC.relative_to(ROOT)} is out of date; run scripts/openapi/studio_components.py", file=sys.stderr)
            return 1
        print("studio.openapi.yaml components are up to date")
        return 0
    SPEC.write_text(updated, encoding="utf-8")
    print(f"wrote {SPEC.relative_to(ROOT)} ({len(spec['components']['schemas'])} schemas, {len(spec['paths'])} paths)")
    return 0


def refs(node):
    if isinstance(node, dict):
        for k, v in node.items():
            if k == "$ref" and isinstance(v, str) and v.startswith("#/components/schemas/"):
                yield v
            else:
                yield from refs(v)
    elif isinstance(node, list):
        for v in node:
            yield from refs(v)


if __name__ == "__main__":
    sys.exit(main())
