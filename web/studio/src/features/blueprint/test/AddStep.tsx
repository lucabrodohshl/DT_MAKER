/**
 * "Add a step" panel of the Scenario Builder, at the insertion point of the scenario.
 *
 * Events: the kernel's availability at that point (POST .../timing) — AVAILABLE NOW, AVAILABLE
 * LATER (with every legal interval) or NOT REACHABLE from the current state — and why each
 * window is what it is (source invariant, guard, target invariant after resets). Adding an
 * event asks the kernel again; when it refuses, the panel says why and offers the legal times
 * it reported ("Move to earliest/latest legal time") or "Choose another event".
 * Studio computes no timing itself.
 */
import {
  Clock,
  Eye,
  FlaskConical,
  Globe2,
  HelpCircle,
  Plus,
  Radio,
  Timer,
} from "lucide-react";
import { useEffect, useRef, useState, type ReactNode } from "react";
import { blueprintApi } from "@/api/blueprints";
import { ApiError } from "@/api/client";
import type { Availability, ScenarioStep, TimingResult } from "@/api/types";
import { Button, Callout, Segmented, StatusBadge, Tabs } from "@/design";
import { useEditor } from "../editor";
import { useModelFacts } from "../formalFacts";
import { refusalFixes, reasonsOf, stepId } from "./scenario";

type Tab = "event" | "delay" | "world" | "observe" | "expect";

const ORIGIN: Record<string, string> = {
  source_invariant: "invariant of the current state",
  guard: "guard of the transition",
  target_invariant: "invariant of the target state (after resets)",
};

function intervalText(a: Availability): string {
  if (a.intervals.length === 0) return "never from here";
  return a.intervals
    .map((iv) =>
      iv.latest_at
        ? iv.latest_at.text === iv.earliest_at.text
          ? `at t = ${iv.earliest_at.text}`
          : `t ∈ [${iv.earliest_at.text}, ${iv.latest_at.text}]`
        : `from t = ${iv.earliest_at.text}, no deadline`,
    )
    .join(" ∪ ");
}

export function WhyWindow({ a, unit }: { a: Availability; unit: string }) {
  return (
    <div className="stack-sm xsmall">
      {a.alternatives.map((alt, i) => (
        <div
          key={i}
          className="stack-sm"
          style={{
            gap: 2,
            paddingLeft: 8,
            borderLeft: "2px solid var(--divider)",
          }}
        >
          <span>
            <span className="mono">{alt.source}</span> →{" "}
            <span className="mono">{alt.target}</span>
            {alt.guard ? (
              <>
                {" "}
                when <code>{alt.guard}</code>
              </>
            ) : null}
            {alt.resets.length > 0 && (
              <span className="subtle"> · resets {alt.resets.join(", ")}</span>
            )}
          </span>
          {alt.window ? (
            <span className="subtle">
              legal delay +{alt.window.earliest.text} …{" "}
              {alt.window.latest ? `+${alt.window.latest.text}` : "∞"} {unit} (t
              = {alt.window.earliest_at.text} …{" "}
              {alt.window.latest_at ? alt.window.latest_at.text : "∞"})
            </span>
          ) : (
            <span style={{ color: "var(--crit)" }}>
              can never fire from here
            </span>
          )}
          {(alt.factors ?? []).map((f, j) => (
            <span key={j} className="subtle">
              • <code>{f.atom}</code> — {ORIGIN[f.origin] ?? f.origin}
              {f.never
                ? ", never satisfiable"
                : f.min_delay
                  ? `, not before +${f.min_delay.text}`
                  : f.max_delay
                    ? `, at most +${f.max_delay.text}`
                    : ""}
              {f.value_now ? ` (now ${f.value_now.text})` : ""}
            </span>
          ))}
        </div>
      ))}
    </div>
  );
}

interface Refusal {
  step: ScenarioStep;
  message: string;
  reasons: string[];
  fixes: ReturnType<typeof refusalFixes>;
  maxDelayAt?: string;
  maxDelay?: string;
}

export function AddStepPanel({
  prefix,
  timing,
  timingError,
  timingPending,
  unit,
  onAdd,
  selectedLabel,
  onSelectLabel,
}: {
  prefix: ScenarioStep[];
  timing: TimingResult | undefined;
  timingError: unknown;
  timingPending: boolean;
  unit: string;
  onAdd: (s: ScenarioStep) => void;
  selectedLabel: string | null;
  onSelectLabel: (l: string | null) => void;
}) {
  const e = useEditor();
  const dt = useModelFacts("dt");
  const pt = useModelFacts("pt");
  const [tab, setTab] = useState<Tab>("event");
  const [level, setLevel] = useState<"dt" | "pt">("dt");
  const [at, setAt] = useState("");
  const [atFor, setAtFor] = useState<string | null>(null);
  const [negative, setNegative] = useState(false);
  const [busy, setBusy] = useState(false);
  const [refusal, setRefusal] = useState<Refusal | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [why, setWhy] = useState(false);
  // other tabs
  const [delay, setDelay] = useState("1");
  const [worldAction, setWorldAction] = useState<
    "set_property" | "add_object" | "remove_object"
  >("set_property");
  const [object, setObject] = useState("");
  const [prop, setProp] = useState("");
  const [value, setValue] = useState("");
  const [obsEvent, setObsEvent] = useState("");
  const [newObj, setNewObj] = useState({
    id: "scenario-object",
    name: "Obstacle",
    semanticType: "obstacle",
    x: "0",
    y: "0",
    w: "1000",
    h: "1000",
  });
  const [telemetry, setTelemetry] = useState<Record<string, string>>({});
  const [expectKind, setExpectKind] = useState<
    | "location"
    | "transition"
    | "window"
    | "monitor"
    | "semantic"
    | "proposition"
    | "world"
  >("location");
  const [expectA, setExpectA] = useState("");
  const [expectB, setExpectB] = useState("");
  const [expectC, setExpectC] = useState("");
  const [expectD, setExpectD] = useState("");

  const refusalRef = useRef<HTMLDivElement>(null);
  useEffect(() => {
    if (refusal)
      refusalRef.current?.scrollIntoView({
        block: "nearest",
        behavior: "smooth",
      });
  }, [refusal]);
  const final = timing?.final;
  const nowText = final?.state.time.text ?? "0";
  const locations = [
    ...new Set((final?.state.configurations ?? []).map((c) => c.location)),
  ];
  const availability = final?.availability ?? [];
  const selected = availability.find((a) => a.label === selectedLabel) ?? null;
  // The time field follows the selected event's earliest legal time until the user types one.
  const defaultAt = selected
    ? selected.status === "now"
      ? nowText
      : (selected.intervals[0]?.earliest_at.text ?? nowText)
    : nowText;
  const effectiveAt = atFor === `${selectedLabel}|${nowText}` ? at : defaultAt;
  const setAtTyped = (v: string) => {
    setAt(v);
    setAtFor(`${selectedLabel}|${nowText}`);
  };
  const ids = prefix.map((s) => s.id);
  const allIds = e.doc.scenarios.flatMap((s) => s.steps.map((x) => x.id));
  const newId = () =>
    stepId([
      ...prefix,
      ...allIds.map((id) => ({ id, kind: "event" as const })),
    ]);

  /** Ask the kernel whether the step is legal after the prefix; add it, or explain the refusal. */
  const tryAdd = async (step: ScenarioStep) => {
    setBusy(true);
    setError(null);
    setRefusal(null);
    try {
      // The kernel decides on the step as an ordinary step; a negative test must be refused.
      const probe: ScenarioStep = { ...step };
      delete probe.expectRefused;
      const r = await blueprintApi.timing(e.id, e.version, {
        scenarioSteps: [...prefix, probe],
      } as Record<string, unknown>);
      const last = r.steps[r.steps.length - 1];
      if (!last) {
        onAdd(step);
        return;
      }
      if (last.status === "not_evaluated") {
        setError(
          "An earlier step of this scenario is refused by the kernel; fix that step first (it is marked in the step list).",
        );
        return;
      }
      if (step.expectRefused) {
        if (last.status === "ok") {
          setError(
            'The kernel admits this event at that time, so it cannot be a "must be refused" step. Clear the option or choose a time the model forbids.',
          );
          return;
        }
        onAdd(step);
        setNegative(false);
        return;
      }
      if (last.status === "ok") {
        onAdd(step);
        return;
      }
      const ex = last.explanation as
        | {
            max_delay?: { text: string } | null;
            max_delay_at?: { text: string } | null;
          }
        | undefined;
      setRefusal({
        step,
        message: last.error?.message ?? "The kernel refused this step.",
        reasons: reasonsOf(last.explanation),
        fixes: refusalFixes(last.explanation, last.requested?.at?.ticks),
        maxDelayAt: ex?.max_delay_at?.text,
        maxDelay: ex?.max_delay?.text,
      });
    } catch (err) {
      setError(
        err instanceof ApiError || err instanceof Error
          ? err.message
          : String(err),
      );
    } finally {
      setBusy(false);
    }
  };

  const addEvent = (label: string, time: string) => {
    const step: ScenarioStep = {
      id: newId(),
      kind: "event",
      label,
      at: time,
      ...(level === "pt" ? { level: "pt" as const } : {}),
      ...(negative ? { expectRefused: true } : {}),
    };
    void tryAdd(step);
  };

  const tabs: { id: Tab; label: ReactNode }[] = [
    { id: "event", label: "Event" },
    { id: "delay", label: "Wait" },
    { id: "world", label: "World" },
    { id: "observe", label: "Telemetry" },
    { id: "expect", label: "Expect" },
  ];
  const groups: { title: string; status: Availability["status"] }[] = [
    { title: "Available now", status: "now" },
    { title: "Available later", status: "later" },
    { title: "Not reachable by waiting", status: "blocked" },
  ];
  const worldObjects = [
    ...e.doc.world.objects.map((o) => ({
      id: o.id,
      name: o.name || o.id,
      props: Object.keys(o.properties ?? {}),
    })),
    ...prefix
      .filter(
        (s) =>
          s.kind === "world" &&
          (s.change as { action?: string } | undefined)?.action ===
            "add_object",
      )
      .map((s) => {
        const o = (
          s.change as {
            object: {
              id: string;
              name?: string;
              properties?: Record<string, unknown>;
            };
          }
        ).object;
        return {
          id: o.id,
          name: `${o.name || o.id} (added by ${s.id})`,
          props: Object.keys(o.properties ?? {}),
        };
      }),
  ];

  return (
    <section className="vts-ed-pane" aria-label="Add a step">
      <div className="vts-ed-pane__head">
        <h3>
          <span className="row">
            <Plus size={14} aria-hidden="true" /> Add a step{" "}
            {ids.length > 0 ? `after ${ids[ids.length - 1]}` : "at the start"}
          </span>
        </h3>
      </div>
      <div className="vts-ed-pane__body stack">
        <div className="row-wrap small">
          <Clock size={14} aria-hidden="true" />
          <span>
            At this point: <strong className="num">t = {nowText}</strong> {unit}
            {locations.length > 0 && (
              <>
                {" "}
                · in <span className="mono">{locations.join(" | ")}</span>
                {locations.length > 1 && (
                  <span className="subtle"> (several possible states)</span>
                )}
              </>
            )}
          </span>
        </div>
        {timingError ? (
          <Callout tone="critical" title="Timing unavailable">
            {timingError instanceof Error
              ? timingError.message
              : "The kernel could not evaluate the scenario so far."}
          </Callout>
        ) : null}
        <Tabs
          value={tab}
          onChange={(t) => {
            setTab(t);
            setRefusal(null);
            setError(null);
          }}
          tabs={tabs}
          label="Step kind"
        />

        {tab === "event" && (
          <div className="stack">
            <Segmented
              value={level}
              onChange={(l) => {
                setLevel(l);
                onSelectLabel(null);
              }}
              label="View of the event"
              options={[
                { id: "dt", label: "Digital Twin View event" },
                { id: "pt", label: "Plant (PT) event" },
              ]}
            />
            {level === "dt" ? (
              timingPending && !timing ? (
                <span className="xsmall subtle">Asking the kernel…</span>
              ) : (
                <div
                  className="vts-sc-avail"
                  role="group"
                  aria-label="Events by availability"
                >
                  {groups.map((g) => {
                    const rows = availability.filter(
                      (a) => a.status === g.status,
                    );
                    if (rows.length === 0) return null;
                    return (
                      <div key={g.status}>
                        <div className="vts-sc-avail__head">{g.title}</div>
                        {rows.map((a) => (
                          <button
                            key={a.label}
                            type="button"
                            className="vts-sc-avail__row"
                            aria-pressed={selectedLabel === a.label}
                            onClick={() =>
                              onSelectLabel(
                                selectedLabel === a.label ? null : a.label,
                              )
                            }
                          >
                            <StatusBadge
                              tone={
                                a.status === "now"
                                  ? "ok"
                                  : a.status === "later"
                                    ? "warning"
                                    : "neutral"
                              }
                              label={
                                a.status === "now"
                                  ? "NOW"
                                  : a.status === "later"
                                    ? "LATER"
                                    : "BLOCKED"
                              }
                            />
                            <span className="mono small truncate">
                              {a.label}
                            </span>
                            <span className="xsmall subtle num">
                              {intervalText(a)}
                            </span>
                          </button>
                        ))}
                      </div>
                    );
                  })}
                  {(final?.unavailable ?? []).length > 0 && (
                    <div>
                      <div className="vts-sc-avail__head">
                        Not reachable from this state
                      </div>
                      {final!.unavailable.map((u) => (
                        <div
                          key={u.label}
                          className="vts-sc-avail__row"
                          style={{ cursor: "default" }}
                        >
                          <StatusBadge tone="neutral" label="NOT REACHABLE" />
                          <span className="mono small truncate">{u.label}</span>
                          <span className="xsmall subtle">
                            only from {u.from_locations.join(", ")}
                          </span>
                        </div>
                      ))}
                    </div>
                  )}
                </div>
              )
            ) : (
              <div className="row-wrap">
                {pt.labels.map((l) => (
                  <button
                    key={l}
                    type="button"
                    className="vts-chip mono"
                    aria-pressed={selectedLabel === l}
                    onClick={() =>
                      onSelectLabel(selectedLabel === l ? null : l)
                    }
                  >
                    {l}
                  </button>
                ))}
                <p className="xsmall subtle" style={{ width: "100%" }}>
                  A plant event is translated to its Digital Twin View
                  counterpart through the alignment&apos;s label equivalence;
                  its legality is decided on the Digital Twin View.
                </p>
              </div>
            )}
            {selectedLabel && (
              <div
                className="stack-sm"
                style={{
                  borderTop: "1px solid var(--divider)",
                  paddingTop: 10,
                }}
              >
                <div className="row-wrap">
                  <label
                    className="vts-f vts-f--inline"
                    style={{ minWidth: 0 }}
                  >
                    <span>
                      <span className="mono">{selectedLabel}</span> at t =
                    </span>
                    <input
                      className="vts-input mono"
                      style={{ width: 110 }}
                      value={effectiveAt}
                      onChange={(x) => setAtTyped(x.target.value.trim())}
                      aria-label="Time of the event"
                      inputMode="decimal"
                    />
                  </label>
                  <span className="xsmall subtle">{unit}</span>
                  {selected && (
                    <button
                      type="button"
                      className="vts-linkbtn xsmall"
                      onClick={() => setWhy((w) => !w)}
                    >
                      <HelpCircle size={12} aria-hidden="true" />{" "}
                      {why ? "Hide why" : "Why this window?"}
                    </button>
                  )}
                </div>
                {why && selected && <WhyWindow a={selected} unit={unit} />}
                <label className="vts-check xsmall">
                  <input
                    type="checkbox"
                    checked={negative}
                    onChange={(x) => setNegative(x.target.checked)}
                  />{" "}
                  Negative test: the verified model must refuse this event at
                  this time
                </label>
                <div className="row">
                  <Button
                    size="sm"
                    variant="primary"
                    icon={<Radio size={13} />}
                    loading={busy}
                    disabled={!effectiveAt || !e.editable}
                    onClick={() => addEvent(selectedLabel, effectiveAt)}
                  >
                    Add event
                  </Button>
                  {selected?.status === "later" && (
                    <span className="xsmall subtle">
                      Earliest legal time preset; a time outside the window is
                      refused by the kernel.
                    </span>
                  )}
                </div>
              </div>
            )}
          </div>
        )}

        {tab === "delay" && (
          <div className="stack-sm">
            <p className="xsmall muted" style={{ margin: 0 }}>
              {final?.max_delay ? (
                <>
                  Time may advance by at most{" "}
                  <strong>+{final.max_delay.text}</strong> (until t ={" "}
                  {final.max_delay_at?.text}) without an event
                  {(final.invariants ?? []).length > 0 && (
                    <>
                      {" "}
                      because of{" "}
                      {final.invariants.map((i) => (
                        <code
                          key={i.location}
                        >{`${i.location}: ${i.invariant}`}</code>
                      ))}
                    </>
                  )}
                  .
                </>
              ) : (
                "No invariant bounds the waiting time in this state."
              )}
            </p>
            <div className="row">
              <label className="vts-f vts-f--inline">
                <span>Wait</span>
                <input
                  className="vts-input mono"
                  style={{ width: 100 }}
                  value={delay}
                  onChange={(x) => setDelay(x.target.value.trim())}
                  inputMode="decimal"
                  aria-label="Delay"
                />
              </label>
              <span className="xsmall subtle">{unit}</span>
              <Button
                size="sm"
                variant="primary"
                icon={<Timer size={13} />}
                loading={busy}
                disabled={!/^\d+(\.\d+)?$/.test(delay) || !e.editable}
                onClick={() =>
                  void tryAdd({ id: newId(), kind: "delay", delay })
                }
              >
                Add wait
              </Button>
            </div>
          </div>
        )}

        {tab === "world" && (
          <div className="stack-sm">
            <p className="xsmall muted" style={{ margin: 0 }}>
              A change of the environment (ground truth). The twin learns it
              only through an observation: choose the data event it produces, if
              any.
            </p>
            <select
              className="vts-select"
              value={worldAction}
              onChange={(x) =>
                setWorldAction(x.target.value as typeof worldAction)
              }
              aria-label="World change"
            >
              <option value="set_property">
                Change a property of an object
              </option>
              <option value="add_object">Add an object</option>
              <option value="remove_object">Remove an object</option>
            </select>
            {worldAction === "add_object" ? (
              <div className="vts-fgrid">
                {(["id", "name", "semanticType"] as const).map((k) => (
                  <label key={k} className="vts-f">
                    <span>
                      {k === "semanticType"
                        ? "Semantic type"
                        : k === "id"
                          ? "Id"
                          : "Name"}
                    </span>
                    <input
                      className="vts-input"
                      value={newObj[k]}
                      onChange={(x) =>
                        setNewObj((o) => ({ ...o, [k]: x.target.value }))
                      }
                    />
                  </label>
                ))}
                {(["x", "y", "w", "h"] as const).map((k) => (
                  <label key={k} className="vts-f">
                    <span>
                      {k} ({e.doc.world.unit})
                    </span>
                    <input
                      className="vts-input mono"
                      value={newObj[k]}
                      onChange={(x) =>
                        setNewObj((o) => ({
                          ...o,
                          [k]: x.target.value.replace(/[^0-9-]/g, ""),
                        }))
                      }
                    />
                  </label>
                ))}
              </div>
            ) : (
              <select
                className="vts-select"
                value={object}
                onChange={(x) => setObject(x.target.value)}
                aria-label="World object"
              >
                <option value="">Choose an object…</option>
                {worldObjects.map((o) => (
                  <option key={o.id} value={o.id}>
                    {o.name}
                  </option>
                ))}
              </select>
            )}
            {worldAction === "set_property" && (
              <div className="vts-fgrid">
                <label className="vts-f">
                  <span>Property</span>
                  <input
                    className="vts-input mono"
                    value={prop}
                    list="vts-sc-props"
                    onChange={(x) => setProp(x.target.value)}
                  />
                  <datalist id="vts-sc-props">
                    {(
                      worldObjects.find((o) => o.id === object)?.props ?? [
                        "observed",
                        "blocked",
                        "state",
                      ]
                    ).map((p) => (
                      <option key={p} value={p} />
                    ))}
                  </datalist>
                </label>
                <label className="vts-f">
                  <span>New value</span>
                  <input
                    className="vts-input mono"
                    value={value}
                    onChange={(x) => setValue(x.target.value)}
                    placeholder="true"
                  />
                </label>
              </div>
            )}
            <div className="vts-fgrid">
              <label className="vts-f">
                <span>At t =</span>
                <input
                  className="vts-input mono"
                  value={effectiveAt}
                  onChange={(x) => setAtTyped(x.target.value.trim())}
                  inputMode="decimal"
                />
              </label>
              <label className="vts-f">
                <span>Observed by the twin as</span>
                <select
                  className="vts-select"
                  value={obsEvent}
                  onChange={(x) => setObsEvent(x.target.value)}
                >
                  <option value="">— not observed —</option>
                  {e.doc.data.events.map((ev) => (
                    <option key={ev.id} value={ev.id}>
                      {ev.label || ev.id}{" "}
                      {ev.formal?.dt
                        ? `(${ev.formal.dt})`
                        : ev.formal?.pt
                          ? `(PT ${ev.formal.pt})`
                          : "(no formal label)"}
                    </option>
                  ))}
                </select>
              </label>
            </div>
            <div className="row">
              <Button
                size="sm"
                variant="primary"
                icon={<Globe2 size={13} />}
                loading={busy}
                disabled={
                  !e.editable ||
                  (worldAction !== "add_object" && !object) ||
                  (worldAction === "set_property" && !prop)
                }
                onClick={() => {
                  const parsed =
                    value === "true"
                      ? true
                      : value === "false"
                        ? false
                        : /^-?\d+$/.test(value)
                          ? Number(value)
                          : value;
                  const change =
                    worldAction === "add_object"
                      ? {
                          action: "add_object",
                          object: {
                            id: newObj.id,
                            layer:
                              e.doc.world.layers.find(
                                (l) => l.role === "ground-truth",
                              )?.id ??
                              e.doc.world.layers[0]?.id ??
                              "default",
                            kind: "rect",
                            semanticType: newObj.semanticType,
                            name: newObj.name,
                            geometry: {
                              x: Number(newObj.x) || 0,
                              y: Number(newObj.y) || 0,
                              w: Number(newObj.w) || 1,
                              h: Number(newObj.h) || 1,
                            },
                            properties: {},
                            tags: ["scenario"],
                          },
                        }
                      : worldAction === "remove_object"
                        ? { action: "remove_object", objectId: object }
                        : {
                            action: "set_property",
                            objectId: object,
                            property: prop,
                            value: parsed,
                          };
                  const step: ScenarioStep = {
                    id: newId(),
                    kind: "world",
                    at: effectiveAt,
                    change,
                    ...(obsEvent
                      ? { observation: { event: obsEvent, at: effectiveAt } }
                      : {}),
                  };
                  if (obsEvent) void tryAdd(step);
                  else onAdd(step);
                }}
              >
                Add world change
              </Button>
            </div>
          </div>
        )}

        {tab === "observe" && (
          <div className="stack-sm">
            <p className="xsmall muted" style={{ margin: 0 }}>
              Telemetry values at a time (context for expectations and monitors;
              observations are not semantic state).
            </p>
            {e.doc.data.telemetry.map((t) => (
              <label key={t.id} className="vts-f vts-f--inline">
                <span className="small" style={{ minWidth: 140 }}>
                  {t.label || t.id}{" "}
                  {t.unit ? <span className="subtle">({t.unit})</span> : null}
                </span>
                <input
                  className="vts-input mono"
                  style={{ width: 120 }}
                  value={telemetry[t.id] ?? ""}
                  onChange={(x) =>
                    setTelemetry((v) => ({ ...v, [t.id]: x.target.value }))
                  }
                  aria-label={`Value of ${t.id}`}
                />
              </label>
            ))}
            <div className="row">
              <label className="vts-f vts-f--inline">
                <span>At t =</span>
                <input
                  className="vts-input mono"
                  style={{ width: 100 }}
                  value={effectiveAt}
                  onChange={(x) => setAtTyped(x.target.value.trim())}
                />
              </label>
              <Button
                size="sm"
                variant="primary"
                icon={<Eye size={13} />}
                disabled={
                  !e.editable || Object.values(telemetry).every((v) => !v)
                }
                onClick={() => {
                  onAdd({
                    id: newId(),
                    kind: "observe",
                    at: effectiveAt,
                    telemetry: Object.fromEntries(
                      Object.entries(telemetry).filter(([, v]) => v !== ""),
                    ),
                  });
                  setTelemetry({});
                }}
              >
                Add observation
              </Button>
            </div>
          </div>
        )}

        {tab === "expect" && (
          <div className="stack-sm">
            <select
              className="vts-select"
              value={expectKind}
              onChange={(x) => {
                setExpectKind(x.target.value as typeof expectKind);
                setExpectA("");
                setExpectB("");
                setExpectC("");
                setExpectD("");
              }}
              aria-label="Expectation"
            >
              <option value="location">The twin is in a state</option>
              <option value="transition">
                The last event took a transition
              </option>
              <option value="window">
                An event is available now / later / not reachable
              </option>
              <option value="monitor">A property monitor has a result</option>
              <option value="semantic">
                The state entails a semantic formula
              </option>
              <option value="proposition">A proposition holds</option>
              <option value="world">The world has a property</option>
            </select>
            {expectKind === "location" && (
              <select
                className="vts-select"
                value={expectA}
                onChange={(x) => setExpectA(x.target.value)}
                aria-label="State"
              >
                <option value="">Choose a state…</option>
                {dt.locations.map((l) => (
                  <option key={l}>{l}</option>
                ))}
              </select>
            )}
            {expectKind === "transition" && (
              <input
                className="vts-input mono"
                value={expectA}
                onChange={(x) => setExpectA(x.target.value)}
                placeholder="SOURCE.label!.TARGET or label!"
                aria-label="Transition"
              />
            )}
            {expectKind === "proposition" && (
              <input
                className="vts-input mono"
                value={expectA}
                onChange={(x) => setExpectA(x.target.value)}
                placeholder="at(STATE)"
                aria-label="Proposition"
              />
            )}
            {expectKind === "semantic" && (
              <input
                className="vts-input mono"
                value={expectA}
                onChange={(x) => setExpectA(x.target.value)}
                placeholder="(> temperature temperature_limit)"
                aria-label="Formula"
              />
            )}
            {expectKind === "window" && (
              <div className="vts-fgrid">
                <select
                  className="vts-select"
                  value={expectA}
                  onChange={(x) => setExpectA(x.target.value)}
                  aria-label="Event"
                >
                  <option value="">Event…</option>
                  {dt.labels.map((l) => (
                    <option key={l}>{l}</option>
                  ))}
                </select>
                <select
                  className="vts-select"
                  value={expectB}
                  onChange={(x) => setExpectB(x.target.value)}
                  aria-label="Availability"
                >
                  <option value="">Availability…</option>
                  <option value="now">available now</option>
                  <option value="later">available later</option>
                  <option value="blocked">not reachable by waiting</option>
                  <option value="unavailable">
                    not reachable from this state
                  </option>
                </select>
                <input
                  className="vts-input mono"
                  value={expectC}
                  onChange={(x) => setExpectC(x.target.value)}
                  placeholder="earliest (+delay, optional)"
                  aria-label="Earliest delay"
                />
                <input
                  className="vts-input mono"
                  value={expectD}
                  onChange={(x) => setExpectD(x.target.value)}
                  placeholder="latest (+delay, optional)"
                  aria-label="Latest delay"
                />
              </div>
            )}
            {expectKind === "monitor" && (
              <div className="vts-fgrid">
                <select
                  className="vts-select"
                  value={expectA}
                  onChange={(x) => setExpectA(x.target.value)}
                  aria-label="Monitor"
                >
                  <option value="">Monitor…</option>
                  {e.doc.assurance.monitors
                    .filter((m) => m.kind === "property")
                    .map((m) => (
                      <option key={m.id} value={m.id}>
                        {m.name || m.id}
                      </option>
                    ))}
                </select>
                <select
                  className="vts-select"
                  value={expectB || "satisfied"}
                  onChange={(x) => setExpectB(x.target.value)}
                  aria-label="Result"
                >
                  <option value="satisfied">satisfied</option>
                  <option value="violated">violated</option>
                  <option value="inconclusive">inconclusive</option>
                </select>
              </div>
            )}
            {expectKind === "world" && (
              <div className="vts-fgrid">
                <select
                  className="vts-select"
                  value={expectA}
                  onChange={(x) => setExpectA(x.target.value)}
                  aria-label="Object"
                >
                  <option value="">Object…</option>
                  {worldObjects.map((o) => (
                    <option key={o.id} value={o.id}>
                      {o.name}
                    </option>
                  ))}
                </select>
                <input
                  className="vts-input mono"
                  value={expectB}
                  onChange={(x) => setExpectB(x.target.value)}
                  placeholder="property"
                  aria-label="Property"
                />
                <input
                  className="vts-input mono"
                  value={expectC}
                  onChange={(x) => setExpectC(x.target.value)}
                  placeholder="equals (e.g. true)"
                  aria-label="Value"
                />
              </div>
            )}
            <div className="row">
              <label className="vts-f vts-f--inline">
                <span>At t =</span>
                <input
                  className="vts-input mono"
                  style={{ width: 100 }}
                  value={effectiveAt}
                  onChange={(x) => setAtTyped(x.target.value.trim())}
                />
              </label>
              <Button
                size="sm"
                variant="primary"
                icon={<FlaskConical size={13} />}
                disabled={!e.editable || !expectA}
                onClick={() => {
                  const parsed =
                    expectC === "true"
                      ? true
                      : expectC === "false"
                        ? false
                        : /^-?\d+$/.test(expectC)
                          ? Number(expectC)
                          : expectC;
                  const expect: Record<string, unknown> =
                    expectKind === "window"
                      ? {
                          window: {
                            label: expectA,
                            ...(expectB ? { status: expectB } : {}),
                            ...(expectC ? { earliest: expectC } : {}),
                            ...(expectD ? { latest: expectD } : {}),
                          },
                        }
                      : expectKind === "monitor"
                        ? {
                            monitor: {
                              id: expectA,
                              status: expectB || "satisfied",
                            },
                          }
                        : expectKind === "world"
                          ? {
                              world: expectB
                                ? {
                                    object: expectA,
                                    property: expectB,
                                    equals: parsed,
                                  }
                                : { object: expectA, exists: true },
                            }
                          : { [expectKind]: expectA };
                  onAdd({
                    id: newId(),
                    kind: "expect",
                    at: effectiveAt,
                    expect,
                  });
                }}
              >
                Add expectation
              </Button>
            </div>
          </div>
        )}

        {error && (
          <Callout tone="critical" title="Not added">
            {error}
          </Callout>
        )}
        {refusal && (
          <div ref={refusalRef}>
            <Callout
              tone="critical"
              title={`Not allowed: ${refusal.step.kind === "delay" ? `waiting ${refusal.step.delay}` : `${refusal.step.label} at t = ${refusal.step.at}`}`}
            >
              <div className="stack-sm">
                <span className="small">{refusal.message}</span>
                {refusal.reasons.length > 0 && (
                  <ul className="small" style={{ margin: 0, paddingLeft: 16 }}>
                    {refusal.reasons.map((r) => (
                      <li key={r}>{r}</li>
                    ))}
                  </ul>
                )}
                <div className="row-wrap">
                  {refusal.fixes.map((f) => (
                    <Button
                      key={f.kind}
                      size="sm"
                      variant={f.kind === "earliest" ? "primary" : "secondary"}
                      onClick={() => void tryAdd({ ...refusal.step, at: f.at })}
                    >
                      Move to {f.kind} legal time (t = {f.at})
                    </Button>
                  ))}
                  {refusal.step.kind === "delay" && refusal.maxDelay && (
                    <Button
                      size="sm"
                      variant="primary"
                      onClick={() =>
                        void tryAdd({
                          ...refusal.step,
                          delay: refusal.maxDelay,
                        })
                      }
                    >
                      Wait only +{refusal.maxDelay} (until t ={" "}
                      {refusal.maxDelayAt})
                    </Button>
                  )}
                  {refusal.step.kind === "event" && (
                    <Button
                      size="sm"
                      onClick={() =>
                        void tryAdd({ ...refusal.step, expectRefused: true })
                      }
                    >
                      Keep it as a negative test
                    </Button>
                  )}
                  <Button
                    size="sm"
                    variant="ghost"
                    onClick={() => {
                      setRefusal(null);
                      onSelectLabel(null);
                    }}
                  >
                    Choose another event
                  </Button>
                </div>
              </div>
            </Callout>
          </div>
        )}
      </div>
    </section>
  );
}
