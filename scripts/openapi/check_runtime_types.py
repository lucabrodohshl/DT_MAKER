#!/usr/bin/env python3
"""Cross-checks web/studio/src/runtime/types.ts against api/runtime.openapi.yaml.

The runtime is owned by twin-runtime and documented by api/runtime.openapi.yaml; Studio's
TypeScript types are a hand-written client view of it. This check makes the client view
answerable to the contract. For every mapped type:

  - each property the client treats as always present (non-optional in TS) must be documented
    and `required` in the OpenAPI schema. When a contract schema declares no `required` list at
    all, presence is unspecified and this is reported as a warning, not a failure;
  - each optional client property must at least be documented;
  - primitive types must be compatible (string / number|integer / boolean / array / object,
    nullability included).

Nested inline objects are checked recursively; $refs on either side are followed.

  python3 scripts/openapi/check_runtime_types.py      # exit 1 on any mismatch
"""
import json
import pathlib
import subprocess
import sys

import yaml

ROOT = pathlib.Path(__file__).resolve().parents[2]
WEB = ROOT / "web" / "studio"
SPEC = yaml.safe_load((ROOT / "api" / "runtime.openapi.yaml").read_text(encoding="utf-8"))
OAS = SPEC["components"]["schemas"]

# TS type -> OpenAPI component (only where the names differ). Types not listed and not present
# in the spec are client-side constructs and are reported as such (not as failures).
MAPPING = {
    "PredictionStep": "TrajectoryStep",
    "ReplayFrameRecord": "ReplayFrame",
    "ReplayResult": "ReplayReport",
    "PlanCandidate": "Candidate",
    "RuntimePackage": "PackageInfo",
}
CLIENT_ONLY = {
    "RuntimeStreamEvent": "client envelope around an SSE event (id, topic, parsed data)",
    "IrConstraint": "fragment of TwinIr, checked through TwinIr",
    "RecordedConfiguration": "fragment of ReplayFrame.fields, checked through ReplayFrame",
    "RecordedBranch": "fragment of ReplayFrame.fields, checked through ReplayFrame",
}


def ts_schemas():
    out = subprocess.run(["node", "scripts/ts-to-jsonschema.mjs", "src/runtime/types.ts"],
                         cwd=WEB, check=True, capture_output=True, text=True).stdout
    return json.loads(out)


TS = ts_schemas()


def deref(schema, pool):
    seen = 0
    while isinstance(schema, dict) and "$ref" in schema and seen < 20:
        schema = pool.get(schema["$ref"].split("/")[-1], {})
        seen += 1
    return schema or {}


def kinds(schema, pool):
    """Set of JSON types a schema admits ('any' if unconstrained)."""
    schema = deref(schema, pool)
    if not schema:
        return {"any"}
    if "anyOf" in schema or "oneOf" in schema:
        r = set()
        for s in schema.get("anyOf", []) + schema.get("oneOf", []):
            r |= kinds(s, pool)
        return r
    if "allOf" in schema:
        return {"object"}
    if "enum" in schema:
        return {type_of(v) for v in schema["enum"]}
    if "const" in schema:
        return {type_of(schema["const"])}
    t = schema.get("type")
    if t is None:
        return {"object"} if "properties" in schema else {"any"}
    ts = set(t) if isinstance(t, list) else {t}
    return {"number" if x == "integer" else x for x in ts}


def type_of(v):
    return {str: "string", bool: "boolean", int: "number", float: "number", type(None): "null"}.get(type(v), "object")


def props(schema, pool):
    schema = deref(schema, pool)
    if "allOf" in schema:
        p, r = {}, set()
        for s in schema["allOf"]:
            sp, sr = props(s, pool)
            p.update(sp)
            r |= sr
        return p, r
    return schema.get("properties", {}), set(schema.get("required", []))


def compare(path, ts_schema, oas_schema, problems, warnings, depth=0):
    ts_k, oas_k = kinds(ts_schema, TS), kinds(oas_schema, OAS)
    if "any" not in ts_k and "any" not in oas_k:
        if not (oas_k - {"null"}) <= (ts_k | {"null"}) and not (ts_k & oas_k):
            problems.append(f"{path}: client expects {sorted(ts_k)}, contract says {sorted(oas_k)}")
            return
        if "null" in oas_k and "null" not in ts_k:
            problems.append(f"{path}: contract allows null, client type does not")
    if depth > 6:
        return
    t, o = deref(ts_schema, TS), deref(oas_schema, OAS)
    if t.get("type") == "array" and o.get("type") == "array":
        compare(path + "[]", t.get("items", {}), o.get("items", {}), problems, warnings, depth + 1)
        return
    tp, treq = props(t, TS)
    if not tp:
        return
    op, oreq = props(o, OAS)
    if not op:
        return  # contract leaves the object open (additionalProperties); nothing to compare
    presence_specified = bool(oreq) or "required" in deref(o, OAS)
    for name, sub in tp.items():
        where = f"{path}.{name}"
        if name not in op:
            problems.append(f"{where}: used by the client but not documented in the contract")
            continue
        if name in treq and name not in oreq:
            (problems if presence_specified else warnings).append(
                f"{where}: client treats it as always present, contract "
                + ("marks it optional" if presence_specified else "does not specify presence"))
        compare(where, sub, op[name], problems, warnings, depth + 1)


def main() -> int:
    problems, warnings, checked = [], [], []
    for ts_name, ts_schema in TS.items():
        if ts_name in CLIENT_ONLY:
            print(f"skip  {ts_name}: {CLIENT_ONLY[ts_name]}")
            continue
        oas_name = MAPPING.get(ts_name, ts_name)
        if oas_name not in OAS:
            problems.append(f"{ts_name}: no schema {oas_name} in api/runtime.openapi.yaml")
            continue
        before = len(problems)
        compare(ts_name, ts_schema, {"$ref": f"#/components/schemas/{oas_name}"}, problems, warnings)
        checked.append(ts_name)
        print(f"{'ok  ' if len(problems) == before else 'FAIL'}  {ts_name} ↔ {oas_name}")
    for p in problems:
        print("  error   " + p)
    if "--verbose" in sys.argv:
        for w in warnings:
            print("  warning " + w)
    print(f"\n{len(checked)} types checked: {len(problems)} error(s), {len(warnings)} warning(s)"
          + ("" if "--verbose" in sys.argv or not warnings else " (--verbose lists them)"))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
