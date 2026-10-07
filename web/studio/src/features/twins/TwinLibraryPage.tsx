/**
 * "Your twins" — the application's landing page. It answers one question: which digital
 * twin do you want to open? Each card summarises a twin (type, operational status, the
 * kernel's current mode, package and verification, alerts, last observation); clicking it
 * opens the twin's workspace. All values come from the backend (runtime and twin-studio).
 */
import { useQueries } from '@tanstack/react-query';
import { ArrowRight, Boxes, FileUp, LayoutTemplate, Plus, Search, Upload, Wand2, Copy } from 'lucide-react';
import { useMemo, useState, type ReactNode } from 'react';
import { Link, useNavigate, useSearchParams } from 'react-router-dom';
import { api } from '@/api/client';
import { useBlueprints } from '@/api/blueprints';
import { keys, useTwins } from '@/api/queries';
import type { AssetDetail, TelemetryChannels, TwinDetail, TwinSummary } from '@/api/types';
import { runtimeApi, runtimeKeys } from '@/runtime/client';
import type { RuntimeState } from '@/runtime/types';
import { Button, Dialog, EmptyState, QueryState, StatusBadge, TimeStamp, TrustBadge, TONE_ICON, toneOf } from '@/design';
import { pluginsFor } from '@/plugins/registry';
import { twinRoute } from '@/app/twinScope';
import { alertCount, overallTrust } from './status';
import './twins.css';

interface Row {
  twin: TwinSummary;
  detail?: TwinDetail;
  asset?: AssetDetail;
  state?: RuntimeState;
  stateError: boolean;
  lastObservedMs?: number;
}

type Filter = 'running' | 'stopped' | 'degraded' | 'alerts' | 'verification';
const FILTERS: { id: Filter; label: string }[] = [
  { id: 'running', label: 'Live' },
  { id: 'stopped', label: 'Not live' },
  { id: 'degraded', label: 'Degraded' },
  { id: 'alerts', label: 'Has alerts' },
  { id: 'verification', label: 'Verification issue' },
];

function modeOf(r: Row) {
  const loc = r.state?.configurations[0]?.location;
  const p = loc ? r.twin.presentation.states?.[loc] : undefined;
  return { loc, label: p?.label ?? loc, tone: toneOf(p?.tone) };
}
const isRunning = (r: Row) => !!r.state && !r.state.closed && !r.state.failed;

function matches(r: Row, f: Filter): boolean {
  switch (f) {
    case 'running': return isRunning(r);
    case 'stopped': return !isRunning(r);
    case 'degraded': return ['warning', 'critical'].includes(modeOf(r).tone) || r.state?.conformance?.status === 'violated';
    case 'alerts': return alertCount(r.state) > 0;
    case 'verification': { const t = overallTrust(r.detail); return !!t && t !== 'pass'; }
  }
}

function TwinCard({ r }: { r: Row }) {
  const { label, tone } = modeOf(r);
  const trust = overallTrust(r.detail);
  const alerts = alertCount(r.state);
  const plugin = pluginsFor(r.twin)[0];
  const to = twinRoute(r.twin.id);
  const running = isRunning(r);
  return (
    <article className="vts-twin-card" aria-label={r.twin.name}>
      <Link to={to} className="vts-twin-card__open" aria-label={`Open ${r.twin.name}`} />
      <div className="vts-twin-card__band" data-tone={running ? tone : 'neutral'} />
      <div className="vts-twin-card__body">
        <div className="vts-twin-card__top">
          <div className="vts-twin-card__icon" aria-hidden="true"><Boxes size={22} /></div>
          <div className="stack-sm" style={{ gap: 2, minWidth: 0 }}>
            <h2 className="vts-twin-card__name truncate">{r.twin.name}</h2>
            <span className="vts-twin-card__type truncate">
              {r.asset?.type ?? 'Twin'}{plugin ? ` · ${plugin.title}` : ''}
            </span>
          </div>
        </div>
        <div className="vts-twin-card__state">
          {!r.twin.runtimeUrl ? (
            <StatusBadge tone="neutral" label="No runtime" />
          ) : r.stateError ? (
            <StatusBadge tone="warning" label="Runtime unreachable" />
          ) : !r.state ? (
            <span className="small muted">Connecting…</span>
          ) : (
            <>
              <span className="vts-twin-card__mode" style={{ color: `var(--${tone === 'ok' ? 'ok' : tone === 'warning' ? 'warn' : tone === 'critical' ? 'crit' : 'text'})` }}>
                {label}
              </span>
              <StatusBadge tone={running ? 'ok' : 'neutral'} icon={running ? TONE_ICON.ok : undefined} label={running ? 'Live' : 'Execution ended'} />
            </>
          )}
        </div>
        <dl className="vts-twin-card__facts">
          <div>
            <dt>Package</dt>
            <dd className="mono small">{r.twin.deployment?.packageId ?? 'not deployed'}</dd>
          </div>
          <div>
            <dt>Verification</dt>
            <dd>{trust ? <TrustBadge state={trust} label={trust === 'pass' ? 'Verified' : 'Verification'} /> : <span className="muted">…</span>}</dd>
          </div>
          <div>
            <dt>Conformance</dt>
            <dd>
              {r.state?.conformance
                ? r.state.conformance.status === 'conformant'
                  ? <StatusBadge tone="ok" label="Conformant" />
                  : <StatusBadge tone="critical" label="Deviation" />
                : <span className="muted small">—</span>}
            </dd>
          </div>
          <div>
            <dt>Alerts</dt>
            <dd>{alerts > 0 ? <StatusBadge tone="warning" label={`${alerts} active`} /> : r.state ? <span className="small">None</span> : <span className="muted small">—</span>}</dd>
          </div>
        </dl>
        <div className="vts-twin-card__foot">
          {r.lastObservedMs ? <>Last observation <TimeStamp value={r.lastObservedMs} relative /></> : <span>No observations yet</span>}
          <span className="vts-twin-card__cta">Open <ArrowRight size={14} aria-hidden="true" style={{ verticalAlign: '-2px' }} /></span>
        </div>
      </div>
    </article>
  );
}

/** "+ Create / Import Twin": the five ways a twin comes into existence. */
export function CreateTwinDialog({ open, onOpenChange, initial }: { open: boolean; onOpenChange: (o: boolean) => void; initial?: 'instantiate' }) {
  const navigate = useNavigate();
  const blueprints = useBlueprints();
  const [step, setStep] = useState<'choose' | 'instantiate'>(initial ?? 'choose');
  const go = (to: string) => {
    onOpenChange(false);
    setStep('choose');
    navigate(to);
  };
  const instantiable = (blueprints.data ?? []).filter((b) => b.published);
  const options: { icon: ReactNode; title: string; text: string; onClick: () => void }[] = [
    { icon: <Wand2 size={20} aria-hidden="true" />, title: 'New Blueprint', text: 'Design a new type of twin in Studio, guided step by step: structure, world, data, behaviour views, semantics, assurance.', onClick: () => go('/studio/new') },
    { icon: <LayoutTemplate size={20} aria-hidden="true" />, title: 'From a template', text: 'Start from a domain template (mobile robot, process equipment…) with asset types, a world palette, a simulator and a data contract.', onClick: () => go('/studio/new?mode=template') },
    { icon: <Copy size={20} aria-hidden="true" />, title: 'Instantiate an existing Blueprint', text: 'Create another concrete twin from a published Blueprint version, with its own assets, identity and deployment.', onClick: () => setStep('instantiate') },
    { icon: <Upload size={20} aria-hidden="true" />, title: 'Import a twin', text: 'Import a Blueprint bundle exported from another Studio, with its formal artefacts.', onClick: () => go('/studio/new?mode=import') },
    { icon: <FileUp size={20} aria-hidden="true" />, title: 'Import formal models', text: 'Bring an existing verified twin: UPPAAL PT and DT views, ontology and interpretations become a new Blueprint.', onClick: () => go('/studio/new?mode=formal') },
  ];
  return (
    <Dialog
      open={open}
      onOpenChange={(o) => {
        onOpenChange(o);
        if (!o) setStep('choose');
      }}
      wide
      title={step === 'choose' ? 'Create or import a twin' : 'Instantiate an existing Blueprint'}
      description={step === 'choose' ? 'Twins are instances of Blueprints: a Blueprint defines a type of twin, is verified and published, then instantiated for each real asset.' : 'Choose the Blueprint; its latest published version is offered by default.'}
    >
      {step === 'choose' ? (
        <div className="vts-choice">
          {options.map((o) => (
            <button key={o.title} type="button" className="vts-choice__opt" onClick={o.onClick}>
              {o.icon}
              <div>
                <strong>{o.title}</strong>
                <span>{o.text}</span>
              </div>
            </button>
          ))}
        </div>
      ) : (
        <div className="stack-sm">
          {blueprints.isPending ? (
            <span className="small muted">Loading Blueprints…</span>
          ) : instantiable.length === 0 ? (
            <EmptyState compact title="No published Blueprint yet" action={<Button size="sm" onClick={() => go('/studio')}>Open Studio</Button>}>
              Publish a Blueprint version first; instances are created from published versions only.
            </EmptyState>
          ) : (
            <div className="vts-choice">
              {instantiable.map((b) => (
                <button key={b.id} type="button" className="vts-choice__opt" onClick={() => go(`/studio/blueprints/${encodeURIComponent(b.id)}/v/${b.published!.version}/release/instances?new=1`)}>
                  <Boxes size={20} aria-hidden="true" />
                  <div>
                    <strong>{b.name}</strong>
                    <span>
                      v{b.published!.version} published · {b.instanceCount} instance{b.instanceCount === 1 ? '' : 's'}
                      {b.description ? ` · ${b.description}` : ''}
                    </span>
                  </div>
                </button>
              ))}
            </div>
          )}
          <div className="row">
            <Button size="sm" variant="ghost" onClick={() => setStep('choose')}>
              Back
            </Button>
          </div>
        </div>
      )}
    </Dialog>
  );
}

export default function TwinLibraryPage() {
  const twins = useTwins();
  const [params, setParams] = useSearchParams();
  const [createOpen, setCreateOpen] = useState(() => params.get('create') !== null);
  const q = params.get('q') ?? '';
  const active = new Set((params.get('filter') ?? '').split(',').filter(Boolean) as Filter[]);
  const sort = params.get('sort') ?? 'name';
  const list = twins.data ?? [];

  const details = useQueries({ queries: list.map((t) => ({ queryKey: keys.twin(t.id), queryFn: () => api.get<TwinDetail>(`/twins/${encodeURIComponent(t.id)}`) })) });
  const states = useQueries({
    queries: list.map((t) => ({
      queryKey: runtimeKeys.state(t.id),
      queryFn: () => runtimeApi(t.id).get<RuntimeState>('/runtime/state'),
      enabled: !!t.runtimeUrl,
      refetchInterval: 4000,
      retry: false,
    })),
  });
  const assets = useQueries({ queries: list.map((t) => ({ queryKey: keys.asset(t.assetId ?? ''), queryFn: () => api.get<AssetDetail>(`/assets/${encodeURIComponent(t.assetId!)}`), enabled: !!t.assetId })) });
  const channels = useQueries({
    queries: list.map((t) => ({
      queryKey: keys.channels(t.assetId ?? ''),
      queryFn: () => api.get<TelemetryChannels>(`/assets/${encodeURIComponent(t.assetId!)}/telemetry`, { descendants: true }),
      enabled: !!t.assetId,
      refetchInterval: 10_000,
    })),
  });

  const rows: Row[] = list.map((twin, i) => ({
    twin,
    detail: details[i]?.data,
    asset: assets[i]?.data,
    state: states[i]?.data,
    stateError: !!states[i]?.isError,
    lastObservedMs: Math.max(0, ...(channels[i]?.data?.channels ?? []).map((c) => c.latest?.observedMs ?? 0)) || undefined,
  }));
  const types = useMemo(() => [...new Set(rows.map((r) => r.asset?.type).filter((x): x is string => !!x))].sort(), [rows]);
  const typeFilter = params.get('type') ?? '';

  const shown = rows
    .filter((r) => !q || `${r.twin.name} ${r.twin.id} ${r.asset?.type ?? ''} ${r.twin.description}`.toLowerCase().includes(q.toLowerCase()))
    .filter((r) => [...active].every((f) => matches(r, f)))
    .filter((r) => !typeFilter || r.asset?.type === typeFilter)
    .sort((a, b) =>
      sort === 'activity'
        ? (b.lastObservedMs ?? 0) - (a.lastObservedMs ?? 0)
        : sort === 'status'
          ? Number(isRunning(b)) - Number(isRunning(a)) || alertCount(b.state) - alertCount(a.state)
          : a.twin.name.localeCompare(b.twin.name),
    );

  const set = (k: string, v: string) => {
    const p = new URLSearchParams(params);
    if (v) p.set(k, v); else p.delete(k);
    setParams(p, { replace: true });
  };
  const toggle = (f: Filter) => {
    const next = new Set(active);
    if (next.has(f)) next.delete(f); else next.add(f);
    set('filter', [...next].join(','));
  };

  const running = rows.filter(isRunning).length;
  const withAlerts = rows.filter((r) => alertCount(r.state) > 0).length;

  return (
    <div className="vts-lib">
      <div className="vts-lib__head">
        <div>
          <h1>Your twins</h1>
          <p>
            {list.length} digital twin{list.length === 1 ? '' : 's'} · {running} live
            {withAlerts > 0 ? ` · ${withAlerts} with alerts` : ''}. Open a twin to monitor, analyse and audit it.
          </p>
        </div>
        <div className="grow" />
        <Button variant="primary" icon={<Plus size={15} />} onClick={() => setCreateOpen(true)}>Create / Import twin</Button>
      </div>
      <div className="vts-lib__tools" role="search">
        <label className="vts-lib__search">
          <Search size={15} aria-hidden="true" />
          <input type="search" value={q} onChange={(e) => set('q', e.target.value)} placeholder="Search twins…" aria-label="Search twins" />
        </label>
        {FILTERS.map((f) => (
          <button key={f.id} type="button" className="vts-chip" aria-pressed={active.has(f.id)} onClick={() => toggle(f.id)}>{f.label}</button>
        ))}
        {types.length > 1 && (
          <select className="vts-select" value={typeFilter} onChange={(e) => set('type', e.target.value)} aria-label="Twin type" style={{ width: 'auto' }}>
            <option value="">All types</option>
            {types.map((t) => <option key={t} value={t}>{t}</option>)}
          </select>
        )}
        <div className="grow" />
        <select className="vts-select" value={sort} onChange={(e) => set('sort', e.target.value)} aria-label="Sort twins" style={{ width: 'auto' }}>
          <option value="name">Sort: name</option>
          <option value="status">Sort: status</option>
          <option value="activity">Sort: last activity</option>
        </select>
      </div>
      <QueryState query={twins} isEmpty={(d) => d.length === 0} empty={
        <EmptyState title="No twins yet">Create a twin in Studio or import an existing definition to get started.</EmptyState>
      }>
        {() =>
          shown.length === 0 ? (
            <EmptyState title="No twin matches">Clear the search or filters to see all {list.length} twins.</EmptyState>
          ) : (
            <div className="vts-lib__grid">
              {shown.map((r) => <TwinCard key={r.twin.id} r={r} />)}
              <button type="button" className="vts-twin-card vts-twin-card--new" onClick={() => setCreateOpen(true)}>
                <Plus size={22} aria-hidden="true" />
                <strong>Create / Import twin</strong>
                <span className="small">Studio, import, or a new instance</span>
              </button>
            </div>
          )
        }
      </QueryState>
      <CreateTwinDialog open={createOpen} onOpenChange={setCreateOpen} initial={params.get('create') === 'instantiate' ? 'instantiate' : undefined} />
      <p className="xsmall subtle" style={{ marginTop: 24 }}>
        Modes, conformance and alerts come from each twin's runtime; verification states from stored evidence.
      </p>
    </div>
  );
}
