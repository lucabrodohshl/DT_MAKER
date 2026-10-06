#!/usr/bin/env python3
"""Validates a RUNNING twin-studio against api/studio.openapi.yaml.

Every GET operation of the spec (except the runtime proxy and the event stream) is called
with identifiers discovered from the server itself, and the 200 response is validated with
JSON Schema 2020-12 against the documented schema. Error responses are checked against the
Error schema with an unknown id. Read-only: nothing is created or changed.

  STUDIO_URL=http://127.0.0.1:8080 python3 scripts/openapi/check_studio_contract.py
"""
import json
import os
import pathlib
import re
import sys
import urllib.error
import urllib.parse
import urllib.request

import yaml
from jsonschema import Draft202012Validator

ROOT = pathlib.Path(__file__).resolve().parents[2]
SPEC = yaml.safe_load((ROOT / "api" / "studio.openapi.yaml").read_text(encoding="utf-8"))
BASE = os.environ.get("STUDIO_URL", "http://127.0.0.1:8080").rstrip("/") + "/api/v1"
SKIP = {"/twins/{twinId}/{family}/{path}", "/stream"}


def get(path: str):
    try:
        with urllib.request.urlopen(BASE + path, timeout=60) as r:
            return r.status, json.loads(r.read() or b"null")
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read() or b"null")


def validator(schema):
    return Draft202012Validator({"components": SPEC["components"], **schema})


def discover():
    """Real identifiers to fill path and required query parameters with."""
    _, twins = get("/twins")
    twin = next((t for t in twins if t.get("assetId")), twins[0])
    _, telemetry = get(f"/assets/{twin['assetId']}/telemetry")
    _, artifacts = get("/artifacts")
    onto = next(a for a in artifacts if a["kind"] == "ontology" and a.get("published"))
    _, onto_detail = get(f"/artifacts/{onto['id']}")
    _, version = get(f"/artifacts/{onto['id']}/versions/{onto['published']['version']}")
    symbol = (version.get("structure") or {}).get("functions", [{}])[0].get("name", "x")
    _, evidence = get("/evidence?limit=1")
    _, changes = get("/changes")
    _, packages = get(f"/packages?twin={twin['id']}")
    refs = [v["ref"] for v in onto_detail["versions"]]
    return {
        "assetId": twin["assetId"], "twinId": twin["id"], "artifactId": onto["id"],
        "version": str(onto["published"]["version"]), "symbol": symbol,
        "channelId": telemetry["channels"][0]["id"] if telemetry["channels"] else None,
        "evidenceId": evidence["items"][0]["id"] if evidence["items"] else None,
        "changeId": changes[0]["id"] if changes else None,
        "packageId": packages[-1]["id"] if packages else None,
        "query": {
            "q": "pump", "ref": onto["published"]["ref"], "from": refs[-1], "to": refs[0],
            "twin": twin["id"], "package": packages[-1]["id"] if packages else "",
        },
    }


def main() -> int:
    ids = discover()
    failures, checked = [], 0
    for path, item in SPEC["paths"].items():
        op = item.get("get")
        if not op or path in SKIP:
            continue
        params = item.get("parameters", []) + op.get("parameters", [])
        params = [SPEC["components"]["parameters"][p["$ref"].split("/")[-1]] if "$ref" in p else p for p in params]
        url, missing = path, None
        for name in re.findall(r"{(\w+)}", path):
            if not ids.get(name):
                missing = name
                break
            url = url.replace("{" + name + "}", urllib.parse.quote(ids[name], safe="@"))
        if missing:
            print(f"SKIP  GET {path} (no {missing} in this data set)")
            continue
        query = {p["name"]: ids["query"][p["name"]] for p in params if p["in"] == "query" and p.get("required")}
        if path == "/evidence/{evidenceId}/status":
            query["twin"] = ids["twinId"]  # one of twin/change is required
        if query:
            url += "?" + urllib.parse.urlencode(query)
        status, body = get(url)
        schema = op["responses"]["200"]["content"]["application/json"]["schema"]
        errors = sorted(validator(schema).iter_errors(body), key=lambda e: list(e.absolute_path))
        checked += 1
        if status != 200 or errors:
            failures.append(url)
            print(f"FAIL  GET {url}: HTTP {status}")
            for e in errors[:5]:
                print(f"        at /{'/'.join(map(str, e.absolute_path))}: {e.message[:200]}")
        else:
            print(f"ok    GET {url}")

    # Errors follow the Error schema.
    error_schema = {"$ref": "#/components/schemas/Error"}
    for url in ["/assets/__no_such_asset__", "/evidence/EV-999999", "/packages/PKG-999999"]:
        status, body = get(url)
        errors = list(validator(error_schema).iter_errors(body))
        checked += 1
        if status != 404 or errors:
            failures.append(url)
            print(f"FAIL  GET {url}: expected 404 + Error, got {status} {errors[:1]}")
        else:
            print(f"ok    GET {url} -> 404 Error")

    print(f"\n{checked - len(failures)}/{checked} responses conform to api/studio.openapi.yaml")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
