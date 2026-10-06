/**
 * Decision provenance: "Why did this happen?" for one ledger record, traced back
 *
 *   decision / event → behavioural transition → semantic propositions
 *   → interpretation → ontology version → raw observation → source asset / sensor
 *   → model / package / deployment version.
 *
 * Every link is read from recorded evidence: the ledger record, the package
 * recorded in it (Studio's registry resolves its artefact versions), the
 * execution's recorded telemetry, and Studio's deployment history.
 */
import { useQuery } from '@tanstack/react-query';
import { Link, useSearchParams } from 'react-router-dom';
import { useChannels, useDeployments, useTwin, useTwins } from '@/api/queries';
import { runtimeApi, useLedger } from '@/runtime/client';
import type { LedgerPage } from '@/runtime/types';
import { ticksToText } from '@/runtime/time';
import { usePackageIr } from '@/features/behavior/useBehavior';
import { Callout, EmptyState, Formula, HashChip, KeyValue, PageHeader, Panel, QueryState, RuntimeUnavailable, StatusBadge, TimeStamp } from '@/design';
import { AssetLink, Crumbs, PackageLink, RefLink } from '@/features/common/links';
import { KIND_LABEL, kindTone, recordSummary, useStudioPackageForHash } from './ledger';
import { useTwinScope } from '@/app/twinScope';

function Link_({ n, title, children }: { n: number; title: string; children: React.ReactNode }) {
  return (
    <li className="vts-chain__item">
      <span className="vts-chain__marker" aria-hidden="true" />
      <div className="stack-sm">
        <h3 style={{ fontSize: 'var(--text-md)' }}>
          {n}. {title}
        </h3>
        <div>{children}</div>
      </div>
    </li>
  );
}

function Chain({ twinId, session, seq }: { twinId: string; session: string; seq: number }) {
  const twin = useTwin(twinId);
  const rec = useQuery({
    queryKey: ['runtime', twinId, 'record', session, seq],
    queryFn: () => runtimeApi(twinId).get<LedgerPage>('/runtime/ledger', { session, since: seq, limit: 1 }),
  });
  const telemetry = useQuery({
    queryKey: ['runtime', twinId, 'exec-telemetry', session],
    queryFn: () => runtimeApi(twinId).get<{ samples: Record<string, unknown>[] }>(`/runtime/executions/${encodeURIComponent(session)}/telemetry`, { max: 2000 }),
    retry: false,
  });
  const record = rec.data?.records.find((r) => r.seq === seq) ?? rec.data?.records[0];
  const pkg = useStudioPackageForHash(twinId, record?.body.package?.hash);
  const ir = usePackageIr(pkg?.id);
  const deployments = useDeployments(twinId);
  const channels = useChannels(twin.data?.assetId ?? undefined);
  if (rec.isPending) return <p className="small muted">Loading record…</p>;
  if (rec.isError || !record) return <Callout tone="critical">Ledger record #{seq} could not be loaded.</Callout>;
  const b = record.body;
  const tpu = ir.data?.time.ticks_per_unit ?? b.time_base ?? twin.data?.ticksPerUnit ?? 1000;
  const branch = b.outcome?.branches?.[0];
  const eventInterp = ir.data?.event_interpretations.find((e) => e.label === (branch?.label ?? b.input?.name));
  const targetProp = ir.data?.propositions.find((p) => p.location === branch?.target);
  const sample = (telemetry.data?.samples ?? []).filter((s) => typeof s.ledger_seq === 'number' && (s.ledger_seq as number) <= seq).at(-1);
  const binding = (role: string) => pkg?.bindings.find((x) => x.role === role);
  const deployedAs = deployments.data?.filter((d) => d.packageHash === b.package?.hash) ?? [];
  const runtimeChannels = channels.data?.channels.filter((c) => c.source.startsWith(`runtime:${twinId}/`)) ?? [];

  return (
    <ol className="vts-chain" style={{ listStyle: 'none', margin: 0, padding: 0 }}>
      <Link_ n={1} title="Decision / event">
        <div className="stack-sm">
          <span className="row-wrap">
            <StatusBadge tone={kindTone(b.kind)} label={KIND_LABEL[b.kind] ?? b.kind} />
            <strong>{recordSummary(record)}</strong>
          </span>
          {b.input && (
            <KeyValue
              compact
              items={[
                ['Input', <span key="i" className="mono">{b.input.name}</span>],
                ['From', b.input.source],
                ['Logical time', `t = ${ticksToText(b.input.at, tpu)} ${twin.data?.presentation.timeUnit ?? ''}`],
                ...(b.input.payload && Object.keys(b.input.payload).length ? [['Stated reason', <span key="r" className="small">{String((b.input.payload as { reason?: string }).reason ?? JSON.stringify(b.input.payload))}</span>] as [string, React.ReactNode]] : []),
              ]}
            />
          )}
        </div>
      </Link_>
      <Link_ n={2} title="Behavioural transition (kernel verdict)">
        {b.kind === 'reject' ? (
          <Callout tone="critical" title="Refused by the kernel">{b.outcome?.error?.message ?? 'Refused'} ({b.outcome?.error?.code})</Callout>
        ) : branch ? (
          <KeyValue
            compact
            items={[
              ['Transition', <span key="t" className="mono">{branch.transition}</span>],
              ['From → to', `${twin.data?.presentation.states?.[branch.source]?.label ?? branch.source} → ${twin.data?.presentation.states?.[branch.target]?.label ?? branch.target}`],
              ['Delay before', b.outcome?.delay !== undefined ? `${ticksToText(b.outcome.delay, tpu)} ${twin.data?.presentation.timeUnit ?? ''}` : '—'],
              ['Clock resets', branch.resets.join(', ') || 'none'],
            ]}
          />
        ) : (
          <span className="small muted">This record is not a transition ({b.kind}).</span>
        )}
      </Link_>
      <Link_ n={3} title="Semantic propositions after the step">
        <span className="mono small">{b.propositions?.join(', ') ?? '—'}</span>
      </Link_>
      <Link_ n={4} title="Interpretation (meaning) in the recorded package">
        {!ir.data ? (
          <span className="small muted">The recorded package is not registered in Studio.</span>
        ) : (
          <div className="stack-sm">
            {eventInterp && (
              <div>
                <span className="xsmall subtle">Event {eventInterp.label}</span>
                <Formula>{eventInterp.formula}</Formula>
              </div>
            )}
            {targetProp && (
              <div>
                <span className="xsmall subtle">{targetProp.id}</span>
                <Formula>{targetProp.interpretation}</Formula>
              </div>
            )}
            {!eventInterp && !targetProp && <span className="small muted">No interpretation applies (internal step).</span>}
          </div>
        )}
      </Link_>
      <Link_ n={5} title="Ontology and interpretation versions (historical, as recorded)">
        {pkg ? (
          <KeyValue
            compact
            items={[
              ['Ontology', binding('ontology') ? <RefLink key="o" refId={binding('ontology')!.ref} /> : '—'],
              ['Ontology hash', <HashChip key="oh" value={binding('ontology')?.sha256} />],
              ['DT interpretation', binding('dt_interpretation') ? <RefLink key="i" refId={binding('dt_interpretation')!.ref} /> : '—'],
              ['Interpretation hash', <HashChip key="ih" value={binding('dt_interpretation')?.sha256} />],
            ]}
          />
        ) : (
          <span className="small muted">Package <HashChip value={b.package?.hash} /> is not registered in Studio.</span>
        )}
      </Link_>
      <Link_ n={6} title="Raw observation received before this step">
        {telemetry.isError ? (
          <span className="small muted">The execution's telemetry is not available.</span>
        ) : sample ? (
          <pre className="vts-code" style={{ maxHeight: 200 }}>{JSON.stringify(sample, null, 1)}</pre>
        ) : (
          <span className="small muted">No telemetry sample was recorded before this step.</span>
        )}
      </Link_>
      <Link_ n={7} title="Source asset and sensors">
        {twin.data?.assetId ? (
          <div className="stack-sm">
            <span className="small">Asset <AssetLink id={twin.data.assetId} /></span>
            {runtimeChannels.length > 0 && (
              <ul className="vts-list">
                {runtimeChannels.map((c) => (
                  <li key={c.id} className="small row-between">
                    <Link to={`/assets/${encodeURIComponent(c.assetId)}/telemetry?channel=${encodeURIComponent(c.id)}`}>{c.presentation?.label ?? c.name}</Link>
                    <span className="mono xsmall subtle">{c.source}</span>
                  </li>
                ))}
              </ul>
            )}
          </div>
        ) : (
          <span className="small muted">The twin is not bound to an asset.</span>
        )}
      </Link_>
      <Link_ n={8} title="Model, package and deployment">
        <KeyValue
          compact
          items={[
            ['Model', `${b.package?.model_id} ${b.package?.model_version}`],
            ['Package', pkg ? <PackageLink key="p" id={pkg.id} /> : <HashChip key="p" value={b.package?.hash} />],
            ['IR hash', <HashChip key="ir" value={b.package?.ir_sha256} />],
            ['Kernel', b.kernel_version],
            ['Deployed as', deployedAs.length ? deployedAs.map((d) => `${d.id} (${d.kind})`).join(', ') : 'No Studio deployment record for this package'],
            ...(deployedAs[0] ? [['Deployed at', <TimeStamp key="d" value={deployedAs[0].deployedAt} />] as [string, React.ReactNode]] : []),
            ['Record hash', <HashChip key="h" value={record.hash} />],
          ]}
        />
      </Link_>
    </ol>
  );
}

export default function ProvenancePage() {
  const [params, setParams] = useSearchParams();
  const scope = useTwinScope();
  const twins = useTwins();
  const twinId = scope?.twin.id ?? params.get('twin') ?? twins.data?.find((t) => t.runtimeUrl)?.id ?? '';
  const session = params.get('session') ?? '';
  const seq = params.get('seq') ? Number(params.get('seq')) : null;
  const twin = twins.data?.find((t) => t.id === twinId);
  const recent = useLedger(twin?.runtimeUrl ? twinId : null, session, 0, 'step', 25);
  const rejects = useLedger(twin?.runtimeUrl ? twinId : null, session, 0, 'reject', 10);
  const choices = [...(recent.data?.records ?? []), ...(rejects.data?.records ?? [])].sort((a, b) => b.seq - a.seq);
  const select = (s: number, sess?: string) => {
    const p = new URLSearchParams(params);
    p.set('twin', twinId);
    if (sess) p.set('session', sess);
    p.set('seq', String(s));
    setParams(p);
  };
  return (
    <div className="vts-page">
      <PageHeader
        eyebrow={<Crumbs items={[{ label: 'Audit', to: '/audit' }, { label: 'Decision provenance' }]} />}
        title="Decision provenance"
        meta={<span>Trace a decision back to the observations, meanings and verified artefacts that produced it</span>}
      />
      <QueryState query={twins}>
        {(list) => (
          <div className="grid-main-side" style={{ gridTemplateColumns: '340px minmax(0, 1fr)' }}>
            <Panel title="Start from a decision" flush>
              <div className="stack" style={{ padding: 'var(--s-3) var(--s-4)' }}>
                {!scope && <select className="vts-select" value={twinId} onChange={(e) => setParams({ twin: e.target.value })} aria-label="Twin">
                  {list.map((t) => <option key={t.id} value={t.id}>{t.name}</option>)}
                </select>}
                {!twin?.runtimeUrl ? (
                  <RuntimeUnavailable compact />
                ) : choices.length === 0 ? (
                  <EmptyState compact title="No decisions recorded yet" />
                ) : (
                  <ul className="vts-list">
                    {choices.map((r) => (
                      <li key={r.seq}>
                        <button type="button" className="vts-link-row" style={{ border: 0, background: seq === r.seq ? 'var(--surface-selected)' : 'transparent', width: '100%', textAlign: 'left', cursor: 'pointer' }} onClick={() => select(r.seq, r.body.session)}>
                          <StatusBadge tone={kindTone(r.body.kind)} label={`#${r.seq}`} />
                          <span className="small truncate">{recordSummary(r)}</span>
                        </button>
                      </li>
                    ))}
                  </ul>
                )}
              </div>
            </Panel>
            <Panel title={seq !== null ? `Provenance of record #${seq}` : 'Provenance'}>
              {seq === null || !twin?.runtimeUrl ? (
                <EmptyState compact title="Select a decision">Choose a transition or refusal to trace it.</EmptyState>
              ) : (
                <Chain twinId={twinId} session={session || recent.data?.session || ''} seq={seq} />
              )}
            </Panel>
          </div>
        )}
      </QueryState>
    </div>
  );
}
