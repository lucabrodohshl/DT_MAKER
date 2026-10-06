/**
 * Pages of the twin workspace that compose existing views for the selected twin:
 * live monitoring, engineering hub, semantics (read-only deployed versions), verification,
 * package, release readiness, replay, and administration (data sources, runtime health,
 * storage). Every page reads the twin from useTwinScope(); nothing here decides anything
 * the backend has not concluded.
 */
import { useMutation } from '@tanstack/react-query';
import { ArrowRight, BookOpen, Cpu, Database, ExternalLink, FileCode2, Gauge, Package as PackageIcon, PencilRuler, ShieldCheck, Workflow } from 'lucide-react';
import { Link, useSearchParams } from 'react-router-dom';
import { useChanges, useChannels, usePipeline } from '@/api/queries';
import type { TwinDetail } from '@/api/types';

type Binding = TwinDetail['bindings'][number];
import { runtimeApi, useExecutions, useRuntimePackage } from '@/runtime/client';
import {
  Button,
  Callout,
  EmptyState,
  ErrorBlock,
  FreshnessBadge,
  HashChip,
  KeyValue,
  LogicalTimeText,
  PageHeader,
  Panel,
  QueryState,
  RuntimeUnavailable,
  Segmented,
  TimeStamp,
  TrustBadge,
  displayUnit,
  formatValue,
} from '@/design';
import { useTwinScope } from '@/app/twinScope';
import { TelemetryTile } from '@/features/assets/AssetOverviewTab';
import { TrustList } from '@/features/common/TrustPanel';
import { ExecutionsPanel } from '@/features/audit/ExecutionsPanel';
import { PipelineView } from '@/features/maintenance/PipelineView';
import VersionPage from '@/features/engineering/VersionPage';
import PackagePage from '@/features/engineering/PackagePage';
import { ConformanceBadge, ModeBadge, OperationalBadge, useLiveStatus } from './status';

const useScope = () => useTwinScope()!;
const binding = (twin: TwinDetail, role: string): Binding | undefined => twin.bindings.find((b) => b.role === role);
const studioRoute = (b: Binding) => {
  const section = b.role.endsWith('model') ? 'models' : b.role === 'ontology' ? 'ontologies' : 'interpretations';
  return `/studio/${section}/${encodeURIComponent(b.artifactId)}/versions/${b.version}`;
};

// ------------------------------------------------------------------ operations › live monitoring
export function TwinLivePage() {
  const { twin, asset, base } = useScope();
  const live = useLiveStatus(twin);
  const channels = useChannels(asset.id, { refetchMs: 3000 });
  const s = live.state;
  return (
    <div className="vts-page stack">
      <PageHeader title="Live monitoring" meta={<span>Current mode, conformance and every telemetry channel of {twin.name}, refreshed live</span>} />
      <Panel title="Now">
        <div className="row-wrap" style={{ gap: 12 }}>
          <OperationalBadge status={live} size="lg" />
          <ModeBadge status={live} size="lg" />
          <ConformanceBadge status={live} />
          {s && <span className="small muted">logical time <LogicalTimeText text={s.time.text} unit={twin.presentation.timeUnit} /></span>}
          {s?.last_transition && (
            <span className="small muted">
              last transition <strong>{s.last_transition.label}</strong> {s.last_transition.from} → {s.last_transition.to}
            </span>
          )}
          <div className="grow" />
          <Link className="small" to={`${base}/behavior`}>Current state <ArrowRight size={12} /></Link>
        </div>
      </Panel>
      <Panel title="Telemetry" subtitle="Every channel of the twin's assets; observation time and freshness on each value" actions={<Link className="small" to={`${base}/operations/telemetry`}>History & charts</Link>}>
        <QueryState query={channels} isEmpty={(d) => d.channels.length === 0} empty={<EmptyState compact title="No telemetry channels" />}>
          {(d) => (
            <div className="grid-3" style={{ gridTemplateColumns: 'repeat(auto-fill, minmax(190px, 1fr))' }}>
              {d.channels.map((c) => <TelemetryTile key={c.id} c={c} assetId={c.assetId} />)}
            </div>
          )}
        </QueryState>
      </Panel>
    </div>
  );
}

// ------------------------------------------------------------------ engineering hub
function DefCard({ icon: Icon, title, b, view, extra }: { icon: typeof Workflow; title: string; b: Binding | undefined; view: string; extra?: React.ReactNode }) {
  return (
    <div className="vts-panel" style={{ padding: 'var(--s-4)' }}>
      <div className="row" style={{ gap: 8, marginBottom: 6 }}><Icon size={16} aria-hidden="true" /> <strong>{title}</strong></div>
      {b ? (
        <div className="stack-sm">
          <Link to={view} className="mono small">{b.ref}</Link>
          {b.versionInfo?.name && <span className="small muted">{b.versionInfo.name}</span>}
          <HashChip value={b.sha256} label="content hash" />
          {extra}
          <div className="row-wrap" style={{ gap: 8 }}>
            <Link className="small" to={view}>View <ArrowRight size={12} /></Link>
            <Link className="small" to={studioRoute(b)}>Open in Studio <ExternalLink size={11} /></Link>
          </div>
        </div>
      ) : <p className="small muted">Not bound.</p>}
    </div>
  );
}

export function TwinEngineeringPage() {
  const { twin, base } = useScope();
  const t = twin.trust;
  return (
    <div className="vts-page stack">
      <PageHeader
        title="Engineering"
        meta={<span>The verified definition {twin.name} runs, the evidence behind it, and how it is deployed</span>}
        actions={<Link className="vts-btn" to={`/studio?twin=${encodeURIComponent(twin.id)}`}><PencilRuler size={14} /> Open definition in Studio</Link>}
      />
      <section className="stack-sm">
        <h2 className="vts-section-title">Definition</h2>
        <div className="grid-2">
          <DefCard icon={Workflow} title="PT view (physical behaviour)" b={binding(twin, 'pt_model')} view={`${base}/engineering/models?view=pt`} />
          <DefCard icon={Workflow} title="DT view (executed by the kernel)" b={binding(twin, 'dt_model')} view={`${base}/engineering/models?view=dt`} />
        </div>
      </section>
      <section className="stack-sm">
        <h2 className="vts-section-title">Semantics</h2>
        <div className="grid-3">
          <DefCard icon={BookOpen} title="Ontology" b={binding(twin, 'ontology')} view={`${base}/engineering/ontology`} />
          <DefCard icon={FileCode2} title="PT interpretation" b={binding(twin, 'pt_interpretation')} view={`${base}/engineering/interpretations?role=pt`} />
          <DefCard icon={FileCode2} title="DT interpretation" b={binding(twin, 'dt_interpretation')} view={`${base}/engineering/interpretations?role=dt`} />
        </div>
      </section>
      <section className="stack-sm">
        <h2 className="vts-section-title">Assurance</h2>
        <div className="grid-3">
          <div className="vts-panel" style={{ padding: 'var(--s-4)' }}>
            <div className="row" style={{ gap: 8, marginBottom: 6 }}><ShieldCheck size={16} aria-hidden="true" /> <strong>Semantic alignment</strong></div>
            <TrustBadge state={t.alignment.state} label="Alignment" />
            <p className="small muted" style={{ margin: '6px 0' }}>{t.alignment.detail}</p>
            <Link className="small" to={`${base}/engineering/verification`}>Evidence <ArrowRight size={12} /></Link>
          </div>
          <div className="vts-panel" style={{ padding: 'var(--s-4)' }}>
            <div className="row" style={{ gap: 8, marginBottom: 6 }}><Cpu size={16} aria-hidden="true" /> <strong>Compiler / Twin IR</strong></div>
            <TrustBadge state={t.compilation.state} label="Compilation" />
            {twin.package && <div style={{ marginTop: 6 }}><HashChip value={twin.package.irSha256} label="IR" /></div>}
            <p className="small muted" style={{ margin: '6px 0' }}>{t.compilation.detail}</p>
          </div>
          <div className="vts-panel" style={{ padding: 'var(--s-4)' }}>
            <div className="row" style={{ gap: 8, marginBottom: 6 }}><PackageIcon size={16} aria-hidden="true" /> <strong>Verified package</strong></div>
            <TrustBadge state={t.packageIntegrity.state} label="Integrity" />
            <div className="mono small" style={{ marginTop: 6 }}>{twin.deployment?.packageId ?? '—'}</div>
            <Link className="small" to={`${base}/engineering/package`}>Package <ArrowRight size={12} /></Link>
          </div>
        </div>
      </section>
      <section className="stack-sm">
        <h2 className="vts-section-title">Deployment</h2>
        <Panel title="Current deployment" actions={<Link className="small" to={`${base}/engineering/deployment`}>History</Link>}>
          {twin.deployment ? (
            <KeyValue compact items={[
              ['Package', <span key="p" className="mono">{twin.deployment.packageId}</span>],
              ['Deployed', <TimeStamp key="t" value={twin.deployment.deployedAt} kind="deployed" showKind />],
              ['By', twin.deployment.deployedBy],
              ['Reason', twin.deployment.reason || '—'],
              ['Runtime compatibility', <TrustBadge key="rc" state={t.runtimeCompatibility.state} label="Runtime" />],
            ]} />
          ) : <p className="small muted">This twin has never been deployed.</p>}
        </Panel>
      </section>
    </div>
  );
}

// ------------------------------------------------------------------ semantics (read-only deployed versions)
export function TwinOntologyPage() {
  const { twin } = useScope();
  const b = binding(twin, 'ontology');
  return b ? <VersionPage artifactRef={b.ref} /> : <div className="vts-page"><EmptyState title="No ontology bound" /></div>;
}

export function TwinInterpretationsPage() {
  const { twin } = useScope();
  const [params, setParams] = useSearchParams();
  const role = params.get('role') === 'pt' ? 'pt' : 'dt';
  const b = binding(twin, `${role}_interpretation`);
  return (
    <div className="stack">
      <div className="vts-page" style={{ paddingBottom: 0 }}>
        <Segmented
          label="Interpretation"
          value={role}
          onChange={(v) => setParams({ role: v }, { replace: true })}
          options={[{ id: 'dt', label: 'DT interpretation' }, { id: 'pt', label: 'PT interpretation' }]}
        />
      </div>
      {b ? <VersionPage key={b.ref} artifactRef={b.ref} /> : <div className="vts-page"><EmptyState title="No interpretation bound for this role" /></div>}
    </div>
  );
}

// ------------------------------------------------------------------ verification
export function TwinVerificationPage() {
  const { twin } = useScope();
  const ids = Object.entries(twin.trust).filter(([, v]) => v.evidenceId);
  return (
    <div className="vts-page stack">
      <PageHeader title="Verification" meta={<span>Trust states of {twin.name}, each computed from stored evidence for exactly the deployed artefacts</span>} />
      <div className="grid-main-side">
        <Panel title="Trust summary"><TrustList twin={twin} /></Panel>
        <Panel title="Evidence">
          {ids.length === 0 ? <p className="small muted">No evidence linked.</p> : (
            <ul className="vts-list">
              {ids.map(([k, v]) => (
                <li key={k} className="small"><span className="muted">{k}</span> · <Link className="mono" to={`/studio/verification/${v.evidenceId}`}>{v.evidenceId}</Link></li>
              ))}
            </ul>
          )}
          <p className="xsmall subtle">Evidence opens in Studio, where every verification run is kept.</p>
        </Panel>
      </div>
    </div>
  );
}

export function TwinPackagePage() {
  const { twin } = useScope();
  return twin.deployment ? <PackagePage id={twin.deployment.packageId} /> : <div className="vts-page"><EmptyState title="Not deployed">No package is deployed for this twin.</EmptyState></div>;
}

// ------------------------------------------------------------------ maintenance › release readiness
function ChangePipeline({ id, title }: { id: string; title: string }) {
  const p = usePipeline(id);
  return (
    <Panel title={title} actions={<Link className="small" to={`/studio/changes/${id}`}>Open in Studio <ExternalLink size={11} /></Link>}>
      <QueryState query={p}>{(d) => <PipelineView pipeline={d} onRun={() => undefined} running={null} disabled />}</QueryState>
    </Panel>
  );
}

export function TwinReadinessPage() {
  const { twin } = useScope();
  const changes = useChanges('open');
  const mine = (changes.data ?? []).filter((c) => c.twinId === twin.id);
  return (
    <div className="vts-page stack">
      <PageHeader title="Release readiness" meta={<span>Release pipeline of every open change of {twin.name}, computed from stored evidence</span>} />
      <QueryState query={changes}>
        {() => mine.length === 0 ? (
          <EmptyState title="No change in progress">Nothing is waiting for release. Start a change in Studio to evolve this twin's definition.</EmptyState>
        ) : (
          <div className="stack">{mine.map((c) => <ChangePipeline key={c.id} id={c.id} title={c.title} />)}</div>
        )}
      </QueryState>
    </div>
  );
}

// ------------------------------------------------------------------ audit › replay
export function TwinReplayPage() {
  const { twin, runtimeConnected } = useScope();
  return (
    <div className="vts-page stack">
      <PageHeader title="Replay" meta={<span>Choose a recorded execution of {twin.name}; it is re-executed with the exact package recorded in its ledger</span>} />
      {runtimeConnected ? <ExecutionsPanel twinId={twin.id} title="Recorded executions" /> : <Panel title="Executions"><RuntimeUnavailable what="Executions and their ledgers are kept by the twin's runtime." /></Panel>}
    </div>
  );
}

// ------------------------------------------------------------------ administration
export function TwinDataSourcesPage() {
  const { twin, asset } = useScope();
  const channels = useChannels(asset.id, { refetchMs: 5000 });
  return (
    <div className="vts-page stack">
      <PageHeader title="Data sources" meta={<span>Where {twin.name}'s observations come from, and whether they are arriving</span>} />
      <Panel title="Telemetry channels" flush>
        <QueryState query={channels} isEmpty={(d) => d.channels.length === 0} empty={<EmptyState compact title="No channels bound" />}>
          {(d) => (
            <div className="vts-table-wrap">
              <table className="vts-table">
                <caption className="sr-only">Telemetry channels and their sources</caption>
                <thead><tr><th>Channel</th><th>Asset</th><th>Source</th><th>Ontology symbol</th><th>Expected period</th><th>Latest</th><th>Freshness</th></tr></thead>
                <tbody>
                  {d.channels.map((c) => (
                    <tr key={c.id}>
                      <td><strong>{c.presentation.label ?? c.name}</strong><div className="mono xsmall subtle">{c.id}</div></td>
                      <td className="small">{c.assetId}</td>
                      <td className="mono xsmall">{c.source}</td>
                      <td className="mono small">{c.ontologySymbol ?? '—'}</td>
                      <td className="small num">{c.expectedPeriodMs ? `${c.expectedPeriodMs / 1000} s` : '—'}</td>
                      <td className="small">
                        {c.latest ? <>{formatValue(c.latest.value, c.presentation.precision)} {displayUnit(c.unit)} · <TimeStamp value={c.latest.observedAt} relative /></> : 'none'}
                      </td>
                      <td>{c.freshness && <FreshnessBadge freshness={c.freshness} />}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          )}
        </QueryState>
      </Panel>
      <p className="xsmall subtle">Sources named <span className="mono">runtime:…</span> are ingested from the twin's runtime stream (reception time is the observation basis); others are pushed to Studio's ingestion API.</p>
    </div>
  );
}

export function TwinRuntimeHealthPage() {
  const { twin, runtimeConnected } = useScope();
  const live = useLiveStatus(twin);
  const pkg = useRuntimePackage(runtimeConnected ? twin.id : null);
  const verify = useMutation({ mutationFn: () => runtimeApi(twin.id).post<{ integrity?: string; checks?: { name: string; passed: boolean }[] }>('/runtime/package/verify', {}) });
  if (!runtimeConnected) return <div className="vts-page"><Panel title="Runtime health"><RuntimeUnavailable what="No twin-runtime is configured for this twin." /></Panel></div>;
  return (
    <div className="vts-page stack">
      <PageHeader title="Runtime health" meta={<span>The twin-runtime executing {twin.name}'s package</span>} actions={<Button icon={<Gauge size={14} />} loading={verify.isPending} onClick={() => verify.mutate()}>Verify running package</Button>} />
      <div className="grid-2">
        <Panel title="Execution">
          <KeyValue compact items={[
            ['Status', <OperationalBadge key="o" status={live} />],
            ['Session', <span key="s" className="mono small">{live.state?.session ?? '—'}</span>],
            ['Ledger records', String(live.state?.ledger.records ?? '—')],
            ['Ledger head', <HashChip key="h" value={live.state?.ledger.head} />],
            ['Failed', live.state ? (live.state.failed ? 'yes' : 'no') : '—'],
          ]} />
        </Panel>
        <Panel title="Running package">
          <QueryState query={pkg}>
            {(p) => (
              <KeyValue compact items={[
                ['Model', `${p.manifest.model.id} ${p.manifest.model.version}`],
                ['Package hash', <HashChip key="ph" value={p.package_hash} />],
                ['IR', <HashChip key="ir" value={p.ir_sha256} />],
                ['Kernel compatibility', p.manifest.kernel_compat],
                ['Checks at load', `${p.checks.filter((c) => c.passed).length}/${p.checks.length} passed`],
              ]} />
            )}
          </QueryState>
        </Panel>
      </div>
      {verify.error && <ErrorBlock error={verify.error} />}
      {verify.data && (
        <Callout tone={verify.data.checks?.every((c) => c.passed) ? 'ok' : 'critical'} title="Package verification by the runtime">
          {verify.data.checks ? `${verify.data.checks.filter((c) => c.passed).length} of ${verify.data.checks.length} checks passed.` : JSON.stringify(verify.data)}
        </Callout>
      )}
    </div>
  );
}

export function TwinStoragePage() {
  const { twin, asset, runtimeConnected } = useScope();
  const execs = useExecutions(runtimeConnected ? twin.id : null);
  const channels = useChannels(asset.id);
  const records = (execs.data ?? []).reduce((n, e) => n + (e.records ?? 0), 0);
  return (
    <div className="vts-page stack">
      <PageHeader title="Storage & retention" meta={<span>What is stored for {twin.name}, and for how long</span>} />
      <div className="grid-2">
        <Panel title="Execution ledgers" subtitle="Kept by the twin's runtime">
          {runtimeConnected ? (
            <QueryState query={execs}>
              {(list) => <KeyValue compact items={[['Executions', String(list.length)], ['Ledger records', String(records)], ['Replayable', `${list.filter((e) => e.replayable).length} of ${list.length}`]]} />}
            </QueryState>
          ) : <RuntimeUnavailable compact />}
        </Panel>
        <Panel title="Telemetry" subtitle="Kept by twin-studio">
          <QueryState query={channels}>{(d) => <KeyValue compact items={[['Channels', String(d.channels.length)], ['Stored as', 'samples with observation and ingestion time and quality']]} />}</QueryState>
        </Panel>
      </div>
      <Callout tone="info" title="Retention">
        <span className="row" style={{ gap: 6 }}><Database size={14} aria-hidden="true" /> No automatic deletion is configured: telemetry, artefact versions, evidence, packages, deployments, engineering audit and execution ledgers are all kept indefinitely in the data directory.</span>
      </Callout>
    </div>
  );
}
