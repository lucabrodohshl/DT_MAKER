/**
 * New Blueprint — steps 1 and 2 of the guided wizard: choose the starting point (blank, a
 * template, a clone of an existing Blueprint, an exported Blueprint bundle, or the formal models
 * of an existing twin: UPPAAL views, ontology, interpretations), then the identity and time base.
 * "Create" makes the first DRAFT on the server (imports are converted and reported there — an
 * unsupported UPPAAL construct is reported, never approximated) and continues with steps 3–11 in
 * the workspace editors.
 */
import { Boxes, Check, Copy, FileCode2, FileUp, LayoutTemplate, Plus, Upload } from 'lucide-react';
import { useMemo, useState, type ReactNode } from 'react';
import { Link, useNavigate, useSearchParams } from 'react-router-dom';
import { blueprintApi, blueprintRoute, useBlueprintTemplates, useBlueprints, usePalettes, useInvalidateBlueprints } from '@/api/blueprints';
import { ApiError } from '@/api/client';
import { Button, Callout, ErrorBlock, StatusBadge } from '@/design';
import { WIZARD_STEPS, startWizard } from './wizard';
import { iconFor } from './icons';
import './blueprint.css';

type Mode = 'blank' | 'template' | 'clone' | 'import' | 'formal';

const MODES: { id: Mode; title: string; text: string; icon: ReactNode }[] = [
  { id: 'blank', title: 'Blank Blueprint', text: 'Start from empty sections and build every layer yourself, guided step by step.', icon: <Plus size={20} aria-hidden="true" /> },
  { id: 'template', title: 'From a template', text: 'Start from a domain template: asset types, a world with its tool palette, a simulator and a data contract.', icon: <LayoutTemplate size={20} aria-hidden="true" /> },
  { id: 'clone', title: 'Clone an existing Blueprint', text: 'Copy another Blueprint version (its sections and formal artefacts) as the first draft of a new one.', icon: <Copy size={20} aria-hidden="true" /> },
  { id: 'import', title: 'Import a twin (Blueprint bundle)', text: 'A twin-blueprint-bundle/1 file exported from another Studio, with its formal artefacts.', icon: <Upload size={20} aria-hidden="true" /> },
  { id: 'formal', title: 'Import formal models', text: 'An existing verified twin: UPPAAL PT and DT views, the ontology and both interpretations.', icon: <FileCode2 size={20} aria-hidden="true" /> },
];

const FORMAL: { key: string; label: string; accept: string }[] = [
  { key: 'ptModel', label: 'Physical System View (UPPAAL .xml, twin-ta .json, TwinTA)', accept: '.xml,.json,.tta,.txt' },
  { key: 'dtModel', label: 'Digital Twin View (UPPAAL .xml, twin-ta .json, TwinTA)', accept: '.xml,.json,.tta,.txt' },
  { key: 'ontology', label: 'Ontology (.ont)', accept: '.ont,.txt' },
  { key: 'ptInterpretation', label: 'PT interpretation (.interp)', accept: '.interp,.txt' },
  { key: 'dtInterpretation', label: 'DT interpretation (.interp)', accept: '.interp,.txt' },
];

const ICONS = ['boxes', 'drone', 'pump', 'thermometer', 'factory', 'cpu', 'robot', 'gauge'];

function slug(s: string) {
  return s
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-+|-+$/g, '')
    .slice(0, 48);
}

export default function NewBlueprintPage() {
  const navigate = useNavigate();
  const [params] = useSearchParams();
  const invalidate = useInvalidateBlueprints();
  const templates = useBlueprintTemplates();
  const blueprints = useBlueprints();
  const palettes = usePalettes();
  const [step, setStep] = useState<1 | 2>(1);
  const [mode, setMode] = useState<Mode>((params.get('mode') as Mode | null) ?? 'blank');
  const [templateId, setTemplateId] = useState(params.get('template') ?? '');
  const [from, setFrom] = useState(params.get('from') ?? '');
  const [fromVersion, setFromVersion] = useState<number | null>(null);
  const [bundle, setBundle] = useState<{ name: string; data: Record<string, unknown> } | null>(null);
  const [bundleError, setBundleError] = useState<string | null>(null);
  const [files, setFiles] = useState<Record<string, { filename: string; content: string }>>({});
  const [name, setName] = useState('');
  const [id, setId] = useState('');
  const [idTouched, setIdTouched] = useState(false);
  const [domain, setDomain] = useState('');
  const [description, setDescription] = useState('');
  const [icon, setIcon] = useState('');
  const [runtimeMode, setRuntimeMode] = useState<'monitor' | 'cosimulation'>('monitor');
  const [timeUnit, setTimeUnit] = useState('s');
  const [ticks, setTicks] = useState('1000');
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<unknown>(null);
  const [report, setReport] = useState<{ id: string; version: number; imports: { role: string; filename: string; imported: boolean; diagnostics?: unknown[]; reason?: string }[] } | null>(null);

  const template = templates.data?.find((t) => t.id === templateId);
  const source = blueprints.data?.find((b) => b.id === from);
  const sourceVersions = source?.versions ?? [];
  const cloneVersion = fromVersion ?? source?.published?.version ?? source?.latest?.version ?? null;
  const domains = useMemo(() => Object.keys(palettes.data?.domains ?? {}).sort(), [palettes.data]);
  const effectiveId = idTouched ? id : slug(name);

  const ready1 =
    mode === 'blank' ||
    (mode === 'template' && !!templateId) ||
    (mode === 'clone' && !!from && cloneVersion !== null) ||
    (mode === 'import' && !!bundle) ||
    (mode === 'formal' && Object.keys(files).length > 0);

  // Prefill identity from the chosen source when moving to step 2.
  const toStep2 = () => {
    if (mode === 'template' && template) {
      if (!name) setName(template.name === 'Empty Blueprint' ? '' : `New ${template.name}`);
      setDomain(template.domain);
      setIcon(template.icon);
      if (template.domain === 'mobile-robot') setRuntimeMode('cosimulation');
    }
    if (mode === 'clone' && source) {
      if (!name) setName(`${source.name} (copy)`);
      setDomain(source.domain);
      setIcon(source.icon);
    }
    if (mode === 'import' && bundle) {
      const meta = (bundle.data.blueprint ?? {}) as { name?: string; domain?: string; icon?: string };
      if (!name && meta.name) setName(meta.name);
      if (meta.domain) setDomain(meta.domain);
      if (meta.icon) setIcon(meta.icon);
    }
    setStep(2);
  };

  const create = async (wizard: boolean) => {
    setBusy(true);
    setError(null);
    try {
      const identity = mode === 'clone' || mode === 'import' ? {} : { identity: { runtimeMode, timeUnit, ticksPerUnit: Number(ticks) || 1000, ...(icon ? { icon } : {}) } };
      const body: Record<string, unknown> = {
        mode,
        name: name.trim(),
        id: effectiveId,
        ...(domain ? { domain } : {}),
        ...(description ? { description } : {}),
        ...(icon ? { icon } : {}),
        ...identity,
        ...(mode === 'template' ? { templateId } : {}),
        ...(mode === 'clone' ? { from, version: cloneVersion } : {}),
        ...(mode === 'import' ? { bundle: bundle?.data } : {}),
        ...(mode === 'formal' ? files : {}),
      };
      const r = await blueprintApi.create(body);
      await invalidate();
      const failed = (r.imports ?? []).filter((x) => !x.imported);
      if (wizard) startWizard(r.blueprintId, r.version);
      if (failed.length > 0) {
        setReport({ id: r.blueprintId, version: r.version, imports: r.imports });
        return;
      }
      navigate(blueprintRoute(r.blueprintId, r.version, wizard ? 'build/structure' : ''));
    } catch (err) {
      setError(err);
    } finally {
      setBusy(false);
    }
  };

  if (report) {
    return (
      <div className="vts-newbp">
        <h1 style={{ margin: 0 }}>Blueprint created — import report</h1>
        <Callout tone="warning" title="Some files were not imported">
          The draft exists; the roles below are empty until you fix or replace the file. Unsupported constructs are never approximated.
        </Callout>
        <table className="vts-table">
          <thead>
            <tr>
              <th>Role</th>
              <th>File</th>
              <th>Result</th>
            </tr>
          </thead>
          <tbody>
            {report.imports.map((x) => (
              <tr key={x.role}>
                <td className="small">{x.role.replace(/_/g, ' ')}</td>
                <td className="mono xsmall">{x.filename}</td>
                <td className="small">
                  {x.imported ? (
                    <StatusBadge tone="ok" label="Imported" />
                  ) : (
                    <>
                      <StatusBadge tone="critical" label="Not imported" /> {x.reason}
                      {(x.diagnostics ?? []).length > 0 && (
                        <ul className="xsmall" style={{ margin: '4px 0 0', paddingLeft: 16 }}>
                          {(x.diagnostics as { code?: string; message?: string }[]).slice(0, 8).map((d, i) => (
                            <li key={i}>
                              <span className="mono">{d.code}</span> {d.message}
                            </li>
                          ))}
                        </ul>
                      )}
                    </>
                  )}
                </td>
              </tr>
            ))}
          </tbody>
        </table>
        <div className="row">
          <span className="grow" />
          <Button variant="primary" onClick={() => navigate(blueprintRoute(report.id, report.version, 'build/structure'))}>
            Continue to the Blueprint
          </Button>
        </div>
      </div>
    );
  }

  return (
    <div className="vts-newbp">
      <div>
        <p className="xsmall subtle" style={{ margin: 0 }}>
          <Link to="/studio">Studio</Link> › New Blueprint
        </p>
        <h1 style={{ margin: '4px 0 0' }}>New Blueprint</h1>
        <p className="muted" style={{ margin: '4px 0 0' }}>
          A Blueprint defines a <strong>type</strong> of twin — its structure, world, data, the physical and twin behaviour views, their meaning and how they are assured. You will create concrete twins (instances) from it once a version is published.
        </p>
      </div>
      <nav className="vts-wizard" aria-label="New Blueprint wizard" style={{ borderRadius: 12, border: '1px solid var(--border)' }}>
        <ol className="vts-wizard__steps">
          {WIZARD_STEPS.map((s) => (
            <li key={s.id}>
              <button type="button" className={`vts-wizard__step${s.n < step ? ' is-done' : ''}`} aria-current={s.n === step ? 'step' : undefined} disabled={s.n > 2 || s.n === step} onClick={() => setStep(s.n as 1 | 2)}>
                <span className="vts-wizard__num">{s.n < step ? <Check size={11} aria-hidden="true" /> : s.n}</span>
                <span className="vts-wizard__label">{s.label}</span>
              </button>
            </li>
          ))}
        </ol>
      </nav>

      {step === 1 ? (
        <section className="stack" aria-labelledby="nb-start">
          <h2 id="nb-start" style={{ margin: 0, fontSize: 'var(--text-lg)' }}>
            1. Starting point
          </h2>
          <div className="vts-newbp__opts vts-choice" role="radiogroup" aria-label="Starting point">
            {MODES.map((m) => (
              <button key={m.id} type="button" role="radio" aria-checked={mode === m.id} aria-pressed={mode === m.id} className="vts-choice__opt" onClick={() => setMode(m.id)}>
                {m.icon}
                <div>
                  <strong>{m.title}</strong>
                  <span>{m.text}</span>
                </div>
              </button>
            ))}
          </div>
          {mode === 'template' && (
            <div className="vts-newbp__tpl vts-choice" role="radiogroup" aria-label="Template">
              {(templates.data ?? []).map((t) => (
                <button key={t.id} type="button" role="radio" aria-checked={templateId === t.id} aria-pressed={templateId === t.id} className="vts-choice__opt" onClick={() => setTemplateId(t.id)}>
                  {iconFor(t.icon, 18)}
                  <div>
                    <strong>{t.name}</strong>
                    <span>{t.description}</span>
                    {t.includes.length > 0 && <span className="xsmall">Includes: {t.includes.join(' · ')}</span>}
                  </div>
                </button>
              ))}
              {templates.isError && <ErrorBlock error={templates.error} compact />}
            </div>
          )}
          {mode === 'clone' && (
            <div className="vts-fgrid">
              <label className="vts-f">
                <span>Blueprint</span>
                <select className="vts-select" value={from} onChange={(x) => { setFrom(x.target.value); setFromVersion(null); }}>
                  <option value="">Choose…</option>
                  {(blueprints.data ?? []).map((b) => (
                    <option key={b.id} value={b.id}>
                      {b.name}
                    </option>
                  ))}
                </select>
              </label>
              <label className="vts-f">
                <span>Version</span>
                <select className="vts-select" value={cloneVersion ?? ''} disabled={!source} onChange={(x) => setFromVersion(Number(x.target.value))}>
                  {sourceVersions.map((v) => (
                    <option key={v.version} value={v.version}>
                      {v.label} ({v.state})
                    </option>
                  ))}
                </select>
              </label>
            </div>
          )}
          {mode === 'import' && (
            <div className="stack-sm">
              <label className="vts-f">
                <span>Blueprint bundle (.json, exported from Release → Package)</span>
                <input
                  type="file"
                  accept=".json,application/json"
                  onChange={async (x) => {
                    const f = x.target.files?.[0];
                    setBundleError(null);
                    setBundle(null);
                    if (!f) return;
                    try {
                      const data = JSON.parse(await f.text()) as Record<string, unknown>;
                      if (data.format !== 'twin-blueprint-bundle/1') throw new Error(`format is "${String(data.format)}", expected "twin-blueprint-bundle/1"`);
                      setBundle({ name: f.name, data });
                    } catch (err) {
                      setBundleError(err instanceof Error ? err.message : String(err));
                    }
                  }}
                />
              </label>
              {bundle && <StatusBadge tone="ok" label={`${bundle.name}: twin-blueprint-bundle/1`} />}
              {bundleError && <Callout tone="critical" title="Not a Blueprint bundle">{bundleError}</Callout>}
            </div>
          )}
          {mode === 'formal' && (
            <div className="stack-sm">
              {FORMAL.map((f) => (
                <label key={f.key} className="vts-f">
                  <span>
                    {f.label} {files[f.key] && <StatusBadge tone="ok" label={files[f.key]!.filename} />}
                  </span>
                  <input
                    type="file"
                    accept={f.accept}
                    onChange={async (x) => {
                      const file = x.target.files?.[0];
                      if (!file) return;
                      const content = await file.text();
                      setFiles((m) => ({ ...m, [f.key]: { filename: file.name, content } }));
                    }}
                  />
                </label>
              ))}
              <p className="xsmall subtle" style={{ margin: 0 }}>
                Models are converted to the canonical timed-automaton form; constructs outside the supported fragment are reported and the role is left empty. Any file can be added later.
              </p>
            </div>
          )}
        </section>
      ) : (
        <section className="stack" aria-labelledby="nb-id">
          <h2 id="nb-id" style={{ margin: 0, fontSize: 'var(--text-lg)' }}>
            2. Identity
          </h2>
          <div className="vts-fgrid">
            <label className="vts-f">
              <span>Name</span>
              <input className="vts-input" value={name} onChange={(x) => setName(x.target.value)} placeholder="Simple Thermal Chamber" autoFocus />
            </label>
            <label className="vts-f">
              <span>Id</span>
              <input
                className="vts-input mono"
                value={effectiveId}
                onChange={(x) => {
                  setIdTouched(true);
                  setId(x.target.value.trim());
                }}
              />
              <span className="vts-f__hint">Lower-case letters, digits and “-”; also the model id of the views.</span>
            </label>
            <label className="vts-f">
              <span>Domain</span>
              <input className="vts-input" value={domain} list="nb-domains" onChange={(x) => setDomain(x.target.value)} placeholder="generic" />
              <datalist id="nb-domains">
                {domains.map((d) => (
                  <option key={d} value={d} />
                ))}
              </datalist>
              <span className="vts-f__hint">Selects the world tool palette (e.g. mobile-robot, process, lab).</span>
            </label>
            <label className="vts-f">
              <span>Icon</span>
              <select className="vts-select" value={icon} onChange={(x) => setIcon(x.target.value)}>
                <option value="">Default</option>
                {ICONS.map((i) => (
                  <option key={i} value={i}>
                    {i}
                  </option>
                ))}
              </select>
            </label>
          </div>
          <label className="vts-f">
            <span>Description</span>
            <textarea className="vts-textarea" rows={2} value={description} onChange={(x) => setDescription(x.target.value)} placeholder="What real systems this twin type represents." />
          </label>
          {mode !== 'clone' && mode !== 'import' && (
            <div className="vts-fgrid">
              <label className="vts-f">
                <span>Runtime mode</span>
                <select className="vts-select" value={runtimeMode} onChange={(x) => setRuntimeMode(x.target.value as 'monitor' | 'cosimulation')}>
                  <option value="monitor">Monitoring — the twin follows events of the real system</option>
                  <option value="cosimulation">Co-simulation — the twin drives a simulated system</option>
                </select>
              </label>
              <label className="vts-f">
                <span>Time unit</span>
                <select className="vts-select" value={timeUnit} onChange={(x) => setTimeUnit(x.target.value)}>
                  <option value="s">seconds</option>
                  <option value="ms">milliseconds</option>
                  <option value="min">minutes</option>
                  <option value="h">hours</option>
                </select>
              </label>
              <label className="vts-f">
                <span>Time resolution (ticks per unit)</span>
                <input className="vts-input mono" value={ticks} onChange={(x) => setTicks(x.target.value.replace(/[^0-9]/g, ''))} />
                <span className="vts-f__hint">Exact logical time: 1000 ticks per {timeUnit} = millisecond precision.</span>
              </label>
            </div>
          )}
          {error !== null && <ErrorBlock error={error} />}
          {error instanceof ApiError && error.status === 409 && <span className="xsmall muted">Choose another id.</span>}
        </section>
      )}

      <div className="row" style={{ borderTop: '1px solid var(--divider)', paddingTop: 12 }}>
        <Link to="/studio" className="vts-btn vts-btn--ghost vts-btn--sm">
          Cancel
        </Link>
        <span className="grow" />
        {step === 2 && (
          <Button size="sm" onClick={() => setStep(1)}>
            Back
          </Button>
        )}
        {step === 1 ? (
          <Button size="sm" variant="primary" disabled={!ready1} onClick={toStep2}>
            Next: identity
          </Button>
        ) : (
          <>
            <Button size="sm" loading={busy} disabled={!name.trim() || !/^[a-z0-9][a-z0-9-]*$/.test(effectiveId)} onClick={() => void create(false)} title="Create the draft and open the full editor (expert path)">
              Create and open editor
            </Button>
            <Button size="sm" variant="primary" icon={<Boxes size={14} />} loading={busy} disabled={!name.trim() || !/^[a-z0-9][a-z0-9-]*$/.test(effectiveId)} onClick={() => void create(true)}>
              Create and continue (guided)
            </Button>
          </>
        )}
      </div>
      {mode === 'formal' && step === 1 && (
        <p className="xsmall subtle" style={{ margin: 0 }}>
          <FileUp size={12} aria-hidden="true" /> Files stay on this machine until you create the Blueprint; Studio stores them as new artefact versions of the draft.
        </p>
      )}
    </div>
  );
}
