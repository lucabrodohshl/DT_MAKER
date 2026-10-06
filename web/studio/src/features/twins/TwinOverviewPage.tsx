/**
 * Twin overview — the landing page of a twin's workspace. It answers, in order:
 * what is this twin, what is happening, is it healthy, which verified definition is running,
 * does anything need attention. A domain view (plugin) gets prominent space when one applies;
 * the generic overview never depends on it.
 */
import { AlertTriangle, ArrowRight, Boxes, Clock, ShieldCheck, Workflow } from 'lucide-react';
import { Link } from 'react-router-dom';
import { useChannels } from '@/api/queries';
import type { TwinDetail } from '@/api/types';
import { useLedger } from '@/runtime/client';
import type { RuntimeState } from '@/runtime/types';
import { Callout, EmptyState, KeyValue, LogicalTimeText, Panel, StatusBadge, TrustBadge, TONE_ICON } from '@/design';
import { useTwinScope } from '@/app/twinScope';
import { PluginHost } from '@/features/assets/AssetPluginTab';
import { TelemetryTile } from '@/features/assets/AssetOverviewTab';
import { KIND_LABEL, kindTone, recordSummary } from '@/features/audit/ledger';
import { pluginsFor } from '@/plugins/registry';
import { ConformanceBadge, ModeBadge, OperationalBadge, alertCount, useLiveStatus } from './status';

const ROLE_LABEL: Record<string, string> = {
  pt_model: 'PT view',
  dt_model: 'DT view',
  ontology: 'Ontology',
  pt_interpretation: 'PT interpretation',
  dt_interpretation: 'DT interpretation',
};

function Summary({ twin, state }: { twin: TwinDetail; state: ReturnType<typeof useLiveStatus> }) {
  const align = twin.trust.alignment;
  return (
    <dl className="vts-ov-summary" aria-label="Twin status summary">
      <div><dt>Status</dt><dd><OperationalBadge status={state} size="lg" /></dd></div>
      <div>
        <dt>Behavioural state</dt>
        <dd>{state.location ? <ModeBadge status={state} size="lg" /> : <span className="muted small">—</span>}</dd>
      </div>
      <div>
        <dt>Logical time</dt>
        <dd className="num">{state.state ? <LogicalTimeText text={state.state.time.text} unit={twin.presentation.timeUnit} /> : <span className="muted small">—</span>}</dd>
      </div>
      <div><dt>Package</dt><dd className="mono">{twin.deployment?.packageId ?? <span className="muted small">not deployed</span>}</dd></div>
      <div><dt>Semantic alignment</dt><dd><TrustBadge state={align.state} label={align.state === 'pass' ? 'Aligned' : 'Alignment'} /></dd></div>
      <div><dt>Conformance</dt><dd>{state.state?.conformance ? <ConformanceBadge status={state} /> : <span className="muted small">—</span>}</dd></div>
    </dl>
  );
}

function Behaviour({ state, twin, base }: { state: RuntimeState; twin: TwinDetail; base: string }) {
  const unit = twin.presentation.timeUnit ?? '';
  const now = state.enabled.filter((e) => e.enabled_now);
  const later = state.enabled.filter((e) => !e.enabled_now);
  const label = (l: string) => twin.presentation.events?.[l]?.label ?? l;
  return (
    <Panel title="Behaviour" subtitle="Committed state of the verified kernel" actions={<Link className="small" to={`${base}/behavior`}>Current state <ArrowRight size={12} /></Link>}>
      <div className="stack">
        {state.last_transition && (
          <div className="small">
            <span className="muted">Latest transition</span>{' '}
            <strong>{label(state.last_transition.label)}</strong>{' '}
            <span className="mono xsmall">{state.last_transition.from} → {state.last_transition.to}</span>{' '}
            <span className="muted">at t = {state.last_transition.at.text} {unit}</span>
          </div>
        )}
        {state.deadline && (
          <div className="small row"><Clock size={13} aria-hidden="true" /> Must leave the current state by t = {state.deadline.text} {unit} (location invariant)</div>
        )}
        <div className="grid-2">
          <div>
            <div className="xsmall subtle" style={{ marginBottom: 4 }}>AVAILABLE NOW</div>
            {now.length ? (
              <ul className="vts-list">{now.map((e) => <li key={`${e.member}-${e.transition}`} className="small"><strong>{label(e.label)}</strong> <span className="muted">→ {e.target}</span></li>)}</ul>
            ) : <p className="small muted">No event can occur right now.</p>}
          </div>
          <div>
            <div className="xsmall subtle" style={{ marginBottom: 4 }}>AVAILABLE LATER</div>
            {later.length ? (
              <ul className="vts-list">
                {later.map((e) => (
                  <li key={`${e.member}-${e.transition}`} className="small">
                    <strong>{label(e.label)}</strong>{' '}
                    <span className="muted">after {e.window.earliest.text}{e.window.latest ? `–${e.window.latest.text}` : '+'} {unit}</span>
                  </li>
                ))}
              </ul>
            ) : <p className="small muted">Nothing becomes enabled by waiting alone.</p>}
          </div>
        </div>
        <Link className="small" to={`${base}/predict/what-if`}>Explore what could happen next in What-if <ArrowRight size={12} /></Link>
      </div>
    </Panel>
  );
}

function Alerts({ state, base }: { state: RuntimeState | undefined; base: string }) {
  const c = state?.conformance;
  const n = alertCount(state);
  return (
    <Panel title="Attention" actions={<Link className="small" to={`${base}/operations/events`}>Events & alerts</Link>}>
      {!c ? (
        <p className="small muted">No runtime verdicts yet.</p>
      ) : c.status !== 'conformant' ? (
        <Callout tone="critical" title="Deviation detected">
          {c.first_violation} <Link to={`${base}/behavior/conformance`}>Conformance</Link>
        </Callout>
      ) : n > 0 ? (
        <Callout tone="warning" title={`${n} alert${n === 1 ? '' : 's'} in this execution`}>
          {c.observations_rejected} refused observation(s), {c.alarms} monitoring alarm(s).
        </Callout>
      ) : (
        <div className="row small"><StatusBadge tone="ok" icon={TONE_ICON.ok} label="Nothing requires attention" /></div>
      )}
      {c && <p className="xsmall subtle" style={{ marginTop: 8 }}>{c.observations} observations and {c.decisions} decisions judged by the kernel in this execution.</p>}
    </Panel>
  );
}

function Definition({ twin, base }: { twin: TwinDetail; base: string }) {
  const order = ['pt_model', 'dt_model', 'ontology', 'pt_interpretation', 'dt_interpretation'];
  const bindings = [...twin.bindings].sort((a, b) => order.indexOf(a.role) - order.indexOf(b.role));
  return (
    <Panel title="Verified definition" subtitle="Exactly what the runtime executes" actions={<Link className="small" to={`${base}/engineering`}>Engineering</Link>}>
      <KeyValue
        compact
        items={[
          ...bindings.map((b): [string, React.ReactNode] => [
            ROLE_LABEL[b.role] ?? b.role,
            <Link key={b.role} className="mono small" to={b.role.endsWith('model') ? `${base}/engineering/models?view=${b.role === 'pt_model' ? 'pt' : 'dt'}` : b.role === 'ontology' ? `${base}/engineering/ontology` : `${base}/engineering/interpretations`}>{b.ref}</Link>,
          ]),
          ['Package', twin.deployment ? <Link key="pkg" className="mono small" to={`${base}/engineering/package`}>{twin.deployment.packageId}</Link> : '—'],
        ]}
      />
      <div className="row-wrap" style={{ marginTop: 10 }}>
        <TrustBadge state={twin.trust.alignment.state} label="Alignment" />
        <TrustBadge state={twin.trust.packageIntegrity.state} label="Package integrity" />
        <TrustBadge state={twin.trust.ontologyRefinement.state} label="Refinement" />
      </div>
    </Panel>
  );
}

function RecentActivity({ twinId, base, connected }: { twinId: string; base: string; connected: boolean }) {
  const ledger = useLedger(connected ? twinId : null, '', 0, '', 8);
  const records = [...(ledger.data?.records ?? [])].reverse();
  return (
    <Panel title="Recent activity" subtitle="Latest records of the current execution's ledger" actions={<Link className="small" to={`${base}/audit/ledger`}>Ledger</Link>}>
      {!connected ? (
        <p className="small muted">Activity is recorded by the twin's runtime, which is not connected.</p>
      ) : records.length === 0 ? (
        <p className="small muted">No records yet.</p>
      ) : (
        <ul className="vts-list">
          {records.map((r) => (
            <li key={r.seq} className="row small" style={{ gap: 8 }}>
              <StatusBadge tone={kindTone(r.body.kind)} label={KIND_LABEL[r.body.kind] ?? r.body.kind} />
              <span className="truncate">{recordSummary(r)}</span>
              <span className="grow" />
              <span className="mono xsmall subtle">#{r.seq}</span>
            </li>
          ))}
        </ul>
      )}
    </Panel>
  );
}

export default function TwinOverviewPage() {
  const scope = useTwinScope()!;
  const { twin, asset, base, runtimeConnected } = scope;
  const live = useLiveStatus(twin);
  const channels = useChannels(asset.id, { refetchMs: 5000 });
  const keyIds = twin.presentation.keyTelemetry ?? [];
  const all = channels.data?.channels ?? [];
  const tiles = (keyIds.length ? keyIds.map((id) => all.find((c) => c.id === id)).filter((c): c is NonNullable<typeof c> => !!c) : all).slice(0, 6);
  const plugin = pluginsFor(twin)[0];
  const tone = live.tone;

  return (
    <div className="vts-page stack">
      {twin.description && <p className="muted" style={{ margin: 0, maxWidth: 900 }}>{twin.description}</p>}
      <Summary twin={twin} state={live} />

      {plugin && (
        <Panel title={plugin.title} subtitle={plugin.description} flush actions={<Link className="small" to={`${base}/assets/${encodeURIComponent(asset.id)}/view/${plugin.id}`}>Full screen</Link>}>
          <div style={{ padding: 'var(--s-3)' }}>
            <PluginHost plugin={plugin} twin={twin} asset={asset} runtimeConnected={runtimeConnected} />
          </div>
        </Panel>
      )}

      <div className="vts-ov-grid">
        <div className="stack">
          <Panel title="Live status" subtitle="Key telemetry (observation time shown on each value)" actions={<Link className="small" to={`${base}/operations/telemetry`}>All telemetry</Link>}>
            {tiles.length ? (
              <div className="grid-3" style={{ gridTemplateColumns: 'repeat(auto-fill, minmax(180px, 1fr))' }}>
                {tiles.map((c) => <TelemetryTile key={c.id} c={c} assetId={asset.id} />)}
              </div>
            ) : (
              <EmptyState compact title="No telemetry channels">This twin's assets have no telemetry channels bound.</EmptyState>
            )}
          </Panel>
          {live.state ? (
            <Behaviour state={live.state} twin={twin} base={base} />
          ) : (
            <Panel title="Behaviour">
              <p className="small muted">{live.operational === 'not_connected' ? 'No runtime is configured for this twin, so there is no live behavioural state.' : 'Waiting for the runtime…'}</p>
            </Panel>
          )}
          <RecentActivity twinId={twin.id} base={base} connected={runtimeConnected} />
        </div>
        <div className="stack">
          <Alerts state={live.state} base={base} />
          <Definition twin={twin} base={base} />
          <Panel title="Assets" actions={<Link className="small" to={`${base}/assets`}>All assets</Link>}>
            <div className="stack-sm">
              <div className="row small"><Boxes size={14} aria-hidden="true" /> <strong>{asset.name}</strong> <span className="muted">{asset.type}</span></div>
              {asset.children.length > 0 ? (
                <ul className="vts-list">
                  {asset.children.slice(0, 6).map((c) => (
                    <li key={c.id} className="small"><Link to={`${base}/assets/${encodeURIComponent(c.id)}`}>{c.name}</Link> <span className="muted">{c.type}</span></li>
                  ))}
                  {asset.children.length > 6 && <li className="small muted">and {asset.children.length - 6} more</li>}
                </ul>
              ) : <p className="small muted">No components recorded.</p>}
              <Link className="small" to={`${base}/knowledge`}>Show in knowledge graph <ArrowRight size={12} /></Link>
            </div>
          </Panel>
          <Panel title="Prediction" actions={<Link className="small" to={`${base}/predict/predictions`}>Predictions</Link>}>
            {live.state ? (
              <div className="stack-sm small">
                <div className="row"><Workflow size={14} aria-hidden="true" /> {live.state.enabled.length} transition(s) possible from the current state{live.state.deterministic ? '' : ' (several configurations)'}.</div>
                <div className="row"><ShieldCheck size={14} aria-hidden="true" /> Every listed future is admissible by the verified model; nothing is guessed.</div>
                {tone === 'critical' && <div className="row"><AlertTriangle size={14} aria-hidden="true" /> The twin is in a critical mode.</div>}
              </div>
            ) : <p className="small muted">Prediction needs the twin's runtime.</p>}
          </Panel>
        </div>
      </div>
    </div>
  );
}
