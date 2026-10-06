/**
 * Asset overview: identity, condition, what the data means, key telemetry, the
 * verified artefacts that define the twin, and recent engineering activity.
 */
import { ArrowRight, Info } from 'lucide-react';
import { Link } from 'react-router-dom';
import { useAudit, useChannels } from '@/api/queries';
import type { TelemetryChannel, TwinDetail } from '@/api/types';
import { useRuntimeState } from '@/runtime/client';
import {
  Callout,
  EmptyState,
  FreshnessBadge,
  HashChip,
  KeyValue,
  LogicalTimeText,
  Panel,
  QueryState,
  RuntimeUnavailable,
  StatusBadge,
  TimeStamp,
  TONE_ICON,
  TruthBadge,
  displayUnit,
  formatValue,
  toneOf,
} from '@/design';
import { PackageLink, RefLink } from '@/features/common/links';
import { TrustList } from '@/features/common/TrustPanel';
import { AuditOperation } from '@/features/audit/auditFormat';
import { useSemanticFacts } from '@/features/semantics/useSemanticFacts';
import { useAssetContext } from './AssetLayout';
import { useTwinScope } from '@/app/twinScope';

export function TelemetryTile({ c, assetId }: { c: TelemetryChannel; assetId: string }) {
  const p = c.presentation ?? {};
  const scope = useTwinScope();
  const to = scope
    ? `${scope.base}/operations/telemetry?channel=${encodeURIComponent(c.id)}`
    : `/assets/${encodeURIComponent(assetId)}/telemetry?channel=${encodeURIComponent(c.id)}`;
  return (
    <Link
      to={to}
      className="vts-panel"
      style={{ padding: 'var(--s-3) var(--s-4)', color: 'inherit', textDecoration: 'none', display: 'block' }}
    >
      <div className="row-between">
        <span className="small muted truncate">{p.label ?? c.name}</span>
        {c.freshness && <FreshnessBadge freshness={c.freshness} />}
      </div>
      <div style={{ fontSize: 'var(--text-xl)', fontWeight: 600 }} className="num">
        {c.latest ? formatValue(c.latest.value, p.precision, undefined) : '—'}
        <span className="small muted" style={{ marginLeft: 4 }}>{displayUnit(c.unit)}</span>
      </div>
      <div className="xsmall subtle">
        {c.latest ? (
          <>
            <TimeStamp value={c.latest.observedAt} kind="observed" showKind relative />
            {c.latest.quality !== 'good' && <> · quality {c.latest.quality}</>}
          </>
        ) : (
          'No observation received'
        )}
      </div>
    </Link>
  );
}

function CurrentCondition({ twin, connected }: { twin: TwinDetail; connected: boolean }) {
  const state = useRuntimeState(connected ? twin.id : null);
  const facts = useSemanticFacts(twin);
  const holding = facts.data?.entries.filter((e) => !e.isEvent && e.truth === 'true') ?? [];
  return (
    <Panel title="Current condition" subtitle="Behavioural mode from the runtime; meaning of observations from the ontology">
      <div className="grid-2">
        <div className="stack-sm">
          <span className="vts-label">Behavioural mode (kernel)</span>
          {!connected ? (
            <RuntimeUnavailable compact />
          ) : state.isError ? (
            <Callout tone="warning">The runtime did not answer: {(state.error as Error).message}</Callout>
          ) : state.data ? (
            <div className="stack-sm">
              {state.data.configurations.map((c) => {
                const p = twin.presentation.states?.[c.location];
                const tone = toneOf(p?.tone);
                return (
                  <div key={c.location} className="stack-sm" style={{ gap: 2 }}>
                    <StatusBadge tone={tone} icon={TONE_ICON[tone]} label={p?.label ?? c.location} size="lg" />
                    {p?.summary && <span className="small muted">{p.summary}</span>}
                  </div>
                );
              })}
              {!state.data.deterministic && (
                <Callout tone="info">The observations so far are consistent with several model states; all are shown.</Callout>
              )}
              <span className="xsmall subtle">
                <LogicalTimeText text={state.data.time.text} unit={twin.presentation.timeUnit} /> · {state.data.enabled.length} transition(s)
                enabled · <Link to="../behavior">Behaviour</Link>
              </span>
            </div>
          ) : (
            <span className="subtle small">Loading…</span>
          )}
        </div>
        <div className="stack-sm">
          <span className="vts-label">What the current data means</span>
          {facts.isError ? (
            <span className="small muted">Meaning could not be evaluated: {(facts.error as Error).message}</span>
          ) : !facts.data ? (
            <span className="subtle small">Evaluating…</span>
          ) : facts.data.observationsConsistent === 'false' ? (
            <Callout tone="critical" title="Observations contradict the ontology">
              The latest readings are inconsistent with the domain axioms (sensor fault or wrong ontology).
            </Callout>
          ) : holding.length === 0 ? (
            <span className="small muted">No location meaning is established by the current observations (insufficient or stale data).</span>
          ) : (
            <ul className="vts-list">
              {holding.map((e) => (
                <li key={e.key} className="row-between">
                  <span className="small">
                    Consistent with <strong>{twin.presentation.states?.[e.key]?.label ?? e.key}</strong>
                  </span>
                  <TruthBadge truth={e.truth} />
                </li>
              ))}
            </ul>
          )}
          {facts.data && facts.data.warnings.length > 0 && (
            <Callout tone="warning" title="Some observations are not fresh">
              <ul style={{ margin: 0, paddingLeft: 16 }}>
                {facts.data.warnings.slice(0, 3).map((w) => <li key={w}>{w}</li>)}
              </ul>
            </Callout>
          )}
          <Link to="../behavior?view=facts" className="small row">
            Why? Semantic facts and evidence <ArrowRight size={12} />
          </Link>
        </div>
      </div>
    </Panel>
  );
}

function KeyTelemetry({ twin, assetId }: { twin: TwinDetail | null; assetId: string }) {
  const channels = useChannels(assetId, { refetchMs: 10_000 });
  return (
    <Panel title="Key telemetry" actions={<Link to="telemetry" className="small">All telemetry</Link>}>
      <QueryState
        query={channels}
        isEmpty={(d) => d.channels.length === 0}
        empty={<EmptyState compact title="No telemetry available">No data source is connected to this asset or its components.</EmptyState>}
      >
        {(d) => {
          const keys = twin?.presentation.keyTelemetry ?? [];
          const shown = (keys.length ? d.channels.filter((c) => keys.includes(c.id)) : d.channels).slice(0, 8);
          return (
            <div className="grid-3" style={{ gridTemplateColumns: 'repeat(auto-fill, minmax(190px, 1fr))' }}>
              {shown.map((c) => <TelemetryTile key={c.id} c={c} assetId={assetId} />)}
            </div>
          );
        }}
      </QueryState>
    </Panel>
  );
}

function VerifiedArtifacts({ twin }: { twin: TwinDetail }) {
  const roleLabel: Record<string, string> = {
    dt_model: 'DT behavioural view',
    pt_model: 'PT view',
    ontology: 'Ontology',
    dt_interpretation: 'DT interpretation',
    pt_interpretation: 'PT interpretation',
  };
  return (
    <Panel title="Verified artefacts defining this twin" subtitle="Exact versions bound by the deployed package">
      {!twin.deployment || !twin.package ? (
        <EmptyState compact title="Not deployed">This twin has no deployed package yet.</EmptyState>
      ) : (
        <div className="stack">
          <KeyValue
            compact
            items={[
              ...['dt_model', 'ontology', 'dt_interpretation', 'pt_model', 'pt_interpretation'].map((role): [string, React.ReactNode] => {
                const b = twin.bindings.find((x) => x.role === role);
                return [roleLabel[role] ?? role, b ? <RefLink refId={b.ref} kind={b.versionInfo?.kind} /> : '—'];
              }),
              ['Package', <PackageLink key="p" id={twin.package.id} />],
              ['Twin IR', <HashChip key="ir" value={twin.package.irSha256} label="IR hash" />],
              ['Deployed', <TimeStamp key="d" value={twin.deployment.deployedAt} kind="deployed" />],
            ]}
          />
          <TrustList twin={twin} />
        </div>
      )}
    </Panel>
  );
}

export default function AssetOverviewTab() {
  const { asset, twin, runtimeConnected } = useAssetContext();
  const audit = useAudit({ subject: twin?.id, limit: 6 });
  const props = Object.entries(asset.properties ?? {});
  return (
    <div className="grid-main-side">
      <div className="stack">
        {twin ? (
          <CurrentCondition twin={twin} connected={runtimeConnected} />
        ) : (
          <Callout tone="neutral" title="No digital twin">
            This asset has no behavioural digital twin; telemetry and relationships are still available.
          </Callout>
        )}
        <KeyTelemetry twin={twin} assetId={twin?.assetId ?? asset.id} />
        {twin && <VerifiedArtifacts twin={twin} />}
      </div>
      <div className="stack">
        <Panel title="Identity">
          <div className="stack">
            {asset.description && <p className="small">{asset.description}</p>}
            <KeyValue
              compact
              items={[
                ['Type', asset.type],
                ['Identifier', <span key="id" className="mono small">{asset.id}</span>],
                ...props.map(([k, v]): [string, React.ReactNode] => [k, String(v)]),
              ]}
            />
            {asset.tags.length > 0 && <div className="row-wrap">{asset.tags.map((t) => <span key={t} className="vts-tag">{t}</span>)}</div>}
          </div>
        </Panel>
        <Panel title="Relationships" actions={<Link to="relationships" className="small">Details</Link>}>
          {asset.relationships.length === 0 && asset.children.length === 0 ? (
            <p className="small muted">No relationships recorded.</p>
          ) : (
            <ul className="vts-list">
              {asset.relationships.slice(0, 6).map((r) => (
                <li key={r.id} className="small">
                  {r.direction === 'outgoing' ? (
                    <>
                      <span className="muted">{r.type}</span> → <Link to={`/assets/${encodeURIComponent(r.targetId)}`}>{r.other?.name ?? r.targetId}</Link>
                    </>
                  ) : (
                    <>
                      <Link to={`/assets/${encodeURIComponent(r.sourceId)}`}>{r.other?.name ?? r.sourceId}</Link> <span className="muted">{r.type}</span> → this
                    </>
                  )}
                </li>
              ))}
              {asset.children.length > 0 && <li className="small muted">Contains {asset.children.length} component(s)</li>}
            </ul>
          )}
        </Panel>
        {twin && (
          <Panel title="Recent engineering activity" actions={<Link to="history" className="small">History</Link>}>
            <QueryState query={audit} isEmpty={(d) => d.items.length === 0} empty={<p className="small muted">No engineering activity recorded for this twin.</p>}>
              {(d) => (
                <ul className="vts-list">
                  {d.items.map((e) => (
                    <li key={e.seq} className="stack-sm" style={{ gap: 2 }}>
                      <AuditOperation record={e} />
                      <span className="xsmall subtle"><TimeStamp value={e.at} relative /></span>
                    </li>
                  ))}
                </ul>
              )}
            </QueryState>
          </Panel>
        )}
        <p className="xsmall subtle row" style={{ gap: 6 }}>
          <Info size={12} aria-hidden="true" /> Operational relationships form the asset graph, which is distinct from the formal ontology.
        </p>
      </div>
    </div>
  );
}
