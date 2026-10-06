#!/usr/bin/env python3
"""Checks the Studio engine routes against api/studio-engine.openapi.yaml.

The C++ test `EngineContract.RecordOneResponsePerRoute` (tests/engine) calls every engine
route with real inputs and writes the route table plus the responses to
<build>/engine-contract-samples.json. This script

  - checks that the route table and the contract list exactly the same operations;
  - validates every recorded response body against its documented 200 schema
    (JSON Schema 2020-12, $refs resolved inside the contract).

  python3 scripts/openapi/check_engine_contract.py [build/studio-release]
"""
import json
import pathlib
import re
import sys

import yaml
from jsonschema import Draft202012Validator

ROOT = pathlib.Path(__file__).resolve().parents[2]
BUILD = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build" / "studio-release"
SPEC = yaml.safe_load((ROOT / "api" / "studio-engine.openapi.yaml").read_text(encoding="utf-8"))
SAMPLES = BUILD / "engine-contract-samples.json"


def to_openapi(path: str) -> str:
    """httplib ':id' parameters -> OpenAPI '{id}', and drop the /api/v1 server prefix."""
    path = re.sub(r":([A-Za-z_]+)", r"{\1}", path)
    return path[len("/api/v1"):] if path.startswith("/api/v1") else path


def main() -> int:
    if not SAMPLES.exists():
        print(f"missing {SAMPLES}: run ctest -R EngineContract in {BUILD} first", file=sys.stderr)
        return 1
    data = json.loads(SAMPLES.read_text(encoding="utf-8"))
    failures = []
    documented = {(m.upper(), p) for p, ops in SPEC["paths"].items() for m in ops if m in ("get", "post", "delete", "put")}
    implemented = {(r["method"], to_openapi(r["path"])) for r in data["routes"]}
    for op in sorted(implemented - documented):
        failures.append(f"route not in the contract: {op[0]} {op[1]}")
    for op in sorted(documented - implemented):
        failures.append(f"contract operation not implemented: {op[0]} {op[1]}")
    for s in data["samples"]:
        path = to_openapi(s["path"])
        op = SPEC["paths"].get(path, {}).get(s["method"].lower())
        if op is None:
            continue
        schema = op["responses"][str(s["status"])]["content"]["application/json"]["schema"]
        validator = Draft202012Validator({"components": SPEC["components"], **schema})
        for err in sorted(validator.iter_errors(s["body"]), key=lambda e: list(e.path)):
            loc = "/".join(str(p) for p in err.path)
            failures.append(f"{s['method']} {path}: {loc}: {err.message[:200]}")
    if failures:
        print("\n".join(failures))
        print(f"\nengine contract: {len(failures)} problem(s)")
        return 1
    print(f"engine contract: {len(implemented)} operations, {len(data['samples'])} responses valid")
    return 0


if __name__ == "__main__":
    sys.exit(main())
