/**
 * Build → Presentation: how Operate shows instances of this twin — names and tones of the DT
 * states and events, key signals, important assets and monitors, charts. This is metadata
 * for generated views, not a low-code builder, and it has no formal effect: it is outside the
 * verified core and changing it invalidates no evidence.
 */
import { Boxes, LayoutDashboard, Plus, Trash2 } from 'lucide-react';
import { useState } from 'react';
import type { StatePresentation, Tone } from '@/api/types';
import { Button, StatusBadge, TONE_ICON, toneOf } from '@/design';
import { useEditor, useSection } from '../editor';
import { useModelFacts } from '../formalFacts';
import { EdPage, JsonSectionView, Pane, SelectField, TextField } from '../ui';
import '@/features/twins/twins.css';
import { useWorkspace } from '../workspace';

const TONES: { value: Tone; label: string }[] = [
  { value: 'neutral', label: 'Neutral' },
  { value: 'ok', label: 'OK (green)' },
  { value: 'info', label: 'Info (blue)' },
  { value: 'warning', label: 'Warning (amber)' },
  { value: 'critical', label: 'Critical (red)' },
];

function MultiPick({ label, options, value, onChange }: { label: string; options: { value: string; label: string }[]; value: string[]; onChange: (v: string[]) => void }) {
  const e = useEditor();
  return (
    <fieldset className="vts-f" style={{ border: 0, padding: 0, margin: 0 }}>
      <legend className="vts-label" style={{ marginBottom: 4 }}>{label}</legend>
      <div className="row-wrap">
        {options.length === 0 && <span className="xsmall subtle">Nothing to choose from yet.</span>}
        {options.map((o) => {
          const on = value.includes(o.value);
          return (
            <button key={o.value} type="button" className="vts-chip" aria-pressed={on} disabled={!e.editable} onClick={() => onChange(on ? value.filter((x) => x !== o.value) : [...value, o.value])}>
              {o.label}
            </button>
          );
        })}
      </div>
    </fieldset>
  );
}

export default function PresentationPage() {
  const e = useEditor();
  const ws = useWorkspace();
  const [p, setP] = useSection('presentation');
  const dt = useModelFacts('dt');
  const [json, setJson] = useState(false);
  const set = <K extends keyof typeof p>(k: K, v: (typeof p)[K], label = `Presentation: ${String(k)}`) => setP((x) => ({ ...x, [k]: v }), { label, key: `pres.${String(k)}` });
  const telemetry = e.doc.data.telemetry.map((t) => ({ value: t.id, label: t.label || t.id }));
  const assets = e.doc.structure.assets.map((a) => ({ value: a.id, label: a.name || a.id }));
  const monitors = e.doc.assurance.monitors.map((m) => ({ value: m.id, label: m.name || m.id }));
  const setState = (loc: string, patch: Partial<StatePresentation>) => set('states', { ...p.states, [loc]: { ...p.states?.[loc], ...patch } }, `State ${loc}`);
  const initial = dt.locations[0];
  const sample = initial ? p.states?.[initial] : undefined;
  return (
    <EdPage
      title="Presentation"
      description="How Operate presents instances of this twin. Views are generated from this metadata; it has no formal effect and is not part of the verified core."
      actions={ws.expert && <Button size="sm" variant="ghost" onClick={() => setJson((j) => !j)}>{json ? 'Form view' : 'JSON view'}</Button>}
      guide={<>Give each Digital Twin View state an operator-friendly name, a tone and a one-line summary; choose the signals shown first. Operate's overview, live monitoring and twin cards use these values — they never change what the twin does.</>}
    >
      <StatusBadge tone="neutral" label="No formal effect — changes invalidate no evidence" />
      {json ? (
        <JsonSectionView value={p} label="Presentation" onApply={(v) => setP(v, { label: 'Edit presentation (JSON)' })} />
      ) : (
        <div className="grid-main-side">
          <div className="stack">
            <Pane title="General">
              <div className="vts-fgrid">
                <TextField label="Display name" value={p.displayName} onChange={(v) => set('displayName', v)} />
                <TextField label="Icon" value={p.icon ?? ''} onChange={(v) => set('icon', v)} />
                <SelectField
                  label="Primary view"
                  value={p.primaryView}
                  onChange={(v) => set('primaryView', v)}
                  options={[
                    { value: 'status', label: 'Status (state, signals, monitors)' },
                    { value: 'topology', label: 'Topology (world diagram)' },
                    { value: 'map', label: 'Map (spatial world)' },
                    { value: 'plugin', label: 'Domain plugin view' },
                  ]}
                />
                <TextField label="Domain plugin" value={p.plugin ?? ''} onChange={(v) => set('plugin', v || null)} hint="Optional (e.g. drone)" />
              </div>
            </Pane>
            <Pane title="Digital Twin View states">
              {!dt.present ? (
                <p className="small muted">Define the Digital Twin View first; its states appear here.</p>
              ) : (
                <table className="vts-table">
                  <thead><tr><th>State</th><th>Label</th><th>Tone</th><th>Summary</th></tr></thead>
                  <tbody>
                    {dt.locations.map((loc) => {
                      const s = p.states?.[loc] ?? {};
                      return (
                        <tr key={loc}>
                          <td className="mono small">{loc}</td>
                          <td><input className="vts-input" value={s.label ?? ''} placeholder={loc} disabled={!e.editable} aria-label={`Label of ${loc}`} onChange={(x) => setState(loc, { label: x.target.value })} /></td>
                          <td>
                            <select className="vts-select" value={s.tone ?? 'neutral'} disabled={!e.editable} aria-label={`Tone of ${loc}`} onChange={(x) => setState(loc, { tone: x.target.value as Tone })}>
                              {TONES.map((t) => <option key={t.value} value={t.value}>{t.label}</option>)}
                            </select>
                          </td>
                          <td><input className="vts-input" value={s.summary ?? ''} disabled={!e.editable} aria-label={`Summary of ${loc}`} onChange={(x) => setState(loc, { summary: x.target.value })} /></td>
                        </tr>
                      );
                    })}
                  </tbody>
                </table>
              )}
            </Pane>
            <Pane title="Digital Twin View events">
              {dt.labels.length === 0 ? (
                <p className="small muted">No events in the Digital Twin View yet.</p>
              ) : (
                <table className="vts-table">
                  <thead><tr><th>Event</th><th>Label shown to operators</th></tr></thead>
                  <tbody>
                    {dt.labels.map((l) => (
                      <tr key={l}>
                        <td className="mono small">{l}</td>
                        <td><input className="vts-input" value={p.events?.[l]?.label ?? ''} placeholder={l.replace(/!$/, '').replace(/_/g, ' ')} disabled={!e.editable} aria-label={`Label of ${l}`} onChange={(x) => set('events', { ...p.events, [l]: { label: x.target.value } }, `Event ${l}`)} /></td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              )}
            </Pane>
            <Pane title="Emphasis">
              <div className="stack">
                <MultiPick label="Key signals" options={telemetry} value={p.keyTelemetry ?? []} onChange={(v) => set('keyTelemetry', v)} />
                <MultiPick label="Important assets" options={assets} value={p.importantAssets ?? []} onChange={(v) => set('importantAssets', v)} />
                <MultiPick label="Important monitors" options={monitors} value={p.importantMonitors ?? []} onChange={(v) => set('importantMonitors', v)} />
                <MultiPick label="Important propositions" options={dt.locations.map((l) => ({ value: `at_${l}`, label: `at_${l}` }))} value={p.importantPropositions ?? []} onChange={(v) => set('importantPropositions', v)} />
              </div>
            </Pane>
            <Pane title="Charts" actions={e.editable && <Button size="sm" icon={<Plus size={13} />} onClick={() => set('charts', [...(p.charts ?? []), { title: 'Chart', telemetry: [] }])}>Chart</Button>}>
              <div className="stack">
                {(p.charts ?? []).map((c, i) => (
                  <div key={i} className="stack-sm" style={{ borderBottom: '1px solid var(--divider)', paddingBottom: 8 }}>
                    <div className="row">
                      <input className="vts-input grow" value={c.title} disabled={!e.editable} aria-label="Chart title" onChange={(x) => set('charts', p.charts.map((y, j) => (j === i ? { ...y, title: x.target.value } : y)))} />
                      <Button size="sm" variant="ghost" iconOnly icon={<Trash2 size={13} />} disabled={!e.editable} onClick={() => set('charts', p.charts.filter((_, j) => j !== i))}>Delete chart</Button>
                    </div>
                    <MultiPick label="Signals" options={telemetry} value={c.telemetry} onChange={(v) => set('charts', p.charts.map((y, j) => (j === i ? { ...y, telemetry: v } : y)))} />
                  </div>
                ))}
                {(p.charts ?? []).length === 0 && <p className="small muted">No charts: Operate shows the key signals.</p>}
              </div>
            </Pane>
          </div>
          <Pane title={<span className="row"><LayoutDashboard size={14} aria-hidden="true" /> Preview in Operate</span>}>
            <article className="vts-twin-card" aria-label="Preview of the twin card">
              <div className="vts-twin-card__band" data-tone={toneOf(sample?.tone)} />
              <div className="vts-twin-card__body">
                <div className="vts-twin-card__top">
                  <div className="vts-twin-card__icon" aria-hidden="true"><Boxes size={22} /></div>
                  <div className="stack-sm" style={{ gap: 2 }}>
                    <h3 className="vts-twin-card__name">{p.displayName || e.doc.identity.name}</h3>
                    <span className="vts-twin-card__type">{e.doc.structure.assetTypes.find((t) => t.id === e.doc.structure.assets.find((a) => a.id === e.doc.structure.root)?.type)?.name ?? 'Twin'}</span>
                  </div>
                </div>
                {initial ? (
                  <div className="vts-twin-card__state">
                    <span className="vts-twin-card__mode">{sample?.label || initial}</span>
                    {(() => {
                      const t = toneOf(sample?.tone);
                      const Icon = TONE_ICON[t];
                      return <StatusBadge tone={t} icon={Icon} label={t === 'neutral' ? 'Initial state' : t} />;
                    })()}
                  </div>
                ) : (
                  <span className="small muted">No states yet</span>
                )}
                {sample?.summary && <p className="small muted">{sample.summary}</p>}
                <dl className="vts-twin-card__facts">
                  {(p.keyTelemetry ?? []).slice(0, 4).map((k) => {
                    const t = e.doc.data.telemetry.find((x) => x.id === k);
                    return (
                      <div key={k}>
                        <dt>{t?.label ?? k}</dt>
                        <dd className="small">— {t?.unit ?? ''}</dd>
                      </div>
                    );
                  })}
                </dl>
              </div>
            </article>
            <p className="xsmall subtle" style={{ marginTop: 8 }}>Initial state shown; live values replace the dashes for running instances.</p>
          </Pane>
        </div>
      )}
    </EdPage>
  );
}
