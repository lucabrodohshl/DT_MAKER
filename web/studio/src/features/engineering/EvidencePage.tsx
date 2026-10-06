/**
 * One evidence record: inputs (exact versions + hashes), checker, verdict, the
 * full machine-readable document, and "Is it still applicable?" (VALID / STALE /
 * INVALIDATED with a structured reason) against a deployment or change.
 */
import { Download } from 'lucide-react';
import { useState } from 'react';
import { Navigate, useParams } from 'react-router-dom';
import { useChanges, useEvidence, useEvidenceStatus, useTwins } from '@/api/queries';
import type { EvidenceRecord } from '@/api/types';
import { Button, Callout, HashChip, KeyValue, OutcomeBadge, PageHeader, Panel, QueryState, StatusBadge, TimeStamp, TrustBadge, downloadText, humanize } from '@/design';
import { Crumbs, RefLink } from '@/features/common/links';

function AlignmentDoc({ doc }: { doc: Record<string, unknown> }) {
  const v = (doc.verdict ?? {}) as Record<string, unknown>;
  const eq = (doc.label_equivalence ?? []) as { pt?: string; dt?: string[] | string }[];
  const lint = ((doc.lint ?? {}) as { findings?: { severity: string; code: string; message: string }[] }).findings ?? [];
  const ce = (v.counterexample ?? {}) as { pt?: string; dt?: string };
  return (
    <div className="stack">
      <KeyValue
        compact
        items={[
          ['Aligned (Definition 5)', String(v.aligned ?? doc.aligned ?? '?')],
          ['Syntactic baseline (WTB)', String(((doc.syntactic_baseline ?? {}) as { aligned?: boolean }).aligned ?? '?')],
          ['Label pairs |E|', String(v.label_pairs ?? '?')],
          ['Zones PT / DT', `${v.pt_zones ?? '?'} / ${v.dt_zones ?? '?'}`],
          ['Final relation size', String(v.final_relation_size ?? '?')],
          ['SMT calls', String(v.smt_calls ?? '?')],
          ...(ce.pt ? [['Counterexample', `PT ${ce.pt} has no matching DT behaviour${ce.dt ? ` (DT ${ce.dt})` : ''}`] as [string, React.ReactNode]] : []),
        ]}
      />
      {eq.length > 0 && (
        <table className="vts-table">
          <caption>Label equivalence E (PT label ≡Δ DT labels)</caption>
          <thead><tr><th scope="col">PT label</th><th scope="col">Equivalent DT label(s)</th></tr></thead>
          <tbody>{eq.map((e, i) => <tr key={i}><td className="mono small">{e.pt}</td><td className="mono small">{Array.isArray(e.dt) ? e.dt.join(', ') : e.dt}</td></tr>)}</tbody>
        </table>
      )}
      {lint.length > 0 && (
        <Callout tone="warning" title="Lint findings">
          <ul style={{ margin: 0, paddingLeft: 16 }}>{lint.map((l, i) => <li key={i}>{l.code} {l.severity}: {l.message}</li>)}</ul>
        </Callout>
      )}
    </div>
  );
}

function Applicability({ e }: { e: EvidenceRecord }) {
  const twins = useTwins();
  const changes = useChanges('open');
  const [ctx, setCtx] = useState<string>('');
  const value = ctx || (twins.data?.[0] ? `twin:${twins.data[0].id}` : '');
  const [type, id] = value.split(':');
  const status = useEvidenceStatus(e.id, type === 'change' ? { change: id } : { twin: id });
  return (
    <Panel title="Is this evidence still applicable?" subtitle="Recomputed now from the stored input hashes — never cached">
      <div className="stack">
        <select className="vts-select" value={value} onChange={(x) => setCtx(x.target.value)} aria-label="Context">
          {twins.data?.map((t) => <option key={t.id} value={`twin:${t.id}`}>Current deployment of {t.name}</option>)}
          {changes.data?.map((c) => <option key={c.id} value={`change:${c.id}`}>Candidate artefacts of {c.id}: {c.title}</option>)}
        </select>
        <QueryState query={status} compact>
          {(s) => (
            <div className="stack-sm">
              <TrustBadge state={s.state === 'valid' ? 'pass' : s.state === 'stale' ? 'stale' : s.state === 'invalidated' ? 'invalidated' : 'not_applicable'} label={humanize(s.state)} />
              {s.state === 'stale' && (
                <Callout tone="warning" title="Why is this stale?">
                  <ul style={{ margin: 0, paddingLeft: 16 }}>
                    {s.changed.map((c) => (
                      <li key={c.role}>
                        <strong>{c.role}</strong> dependency changed: the evidence examined <RefLink refId={c.evidenceRef} /> (<HashChip value={c.evidenceSha256} length={8} />) but{' '}
                        <RefLink refId={c.currentRef} /> (<HashChip value={c.currentSha256} length={8} />) is used now.
                      </li>
                    ))}
                  </ul>
                </Callout>
              )}
              {s.state !== 'stale' && <ul className="small" style={{ margin: 0, paddingLeft: 16 }}>{s.reasons.map((r) => <li key={r}>{r}</li>)}</ul>}
              <span className="xsmall subtle">Context: {s.context} · evaluated <TimeStamp value={s.evaluatedAt} /></span>
            </div>
          )}
        </QueryState>
      </div>
    </Panel>
  );
}

export default function EvidencePage() {
  const { evidenceId = '' } = useParams();
  const q = useEvidence(evidenceId);
  if (q.data?.kind === 'refinement') return <Navigate to={`/studio/refinement/${evidenceId}`} replace />;
  return (
    <div className="vts-page">
      <QueryState query={q}>
        {(e) => (
          <div className="stack">
            <PageHeader
              eyebrow={<Crumbs items={[{ label: 'Studio', to: '/studio' }, { label: 'Verification history', to: '/studio/verification' }, { label: e.id }]} />}
              title={`${humanize(e.kind)} evidence ${e.id}`}
              meta={<><OutcomeBadge outcome={e.outcome} verdict={e.verdict} /><TimeStamp value={e.createdAt} kind="evidence" showKind /><span>by {e.createdBy}</span></>}
              actions={
                <Button icon={<Download size={14} />} onClick={() => downloadText(`${e.id}.json`, 'application/json', JSON.stringify(e, null, 2))}>
                  Export evidence (JSON)
                </Button>
              }
            />
            <p>{e.summary}</p>
            {e.documentIntegrity === 'fail' && <Callout tone="critical" title="Evidence document integrity failure">The stored document no longer matches its hash.</Callout>}
            <div className="grid-main-side">
              <div className="stack">
                {e.kind === 'alignment' && e.document && <Panel title="Alignment result"><AlignmentDoc doc={e.document} /></Panel>}
                {e.kind === 'package' && e.document && Array.isArray((e.document as { checks?: unknown[] }).checks) && (
                  <Panel title="Package checks" flush>
                    <table className="vts-table">
                      <caption className="sr-only">Checks</caption>
                      <tbody>
                        {((e.document as { checks: { name: string; passed: boolean; detail: string }[] }).checks).map((c) => (
                          <tr key={c.name}><td className="small">{c.name}</td><td><StatusBadge tone={c.passed ? 'ok' : 'critical'} label={c.passed ? 'Passed' : 'Failed'} /></td><td className="xsmall subtle">{c.detail}</td></tr>
                        ))}
                      </tbody>
                    </table>
                  </Panel>
                )}
                <Panel title="Evidence document" subtitle="Canonical JSON stored under its SHA-256">
                  <pre className="vts-code" style={{ maxHeight: 520 }}>{JSON.stringify(e.document, null, 2)}</pre>
                </Panel>
              </div>
              <div className="stack">
                <Panel title="Inputs (exact artefacts examined)">
                  <ul className="vts-list">
                    {e.inputs.map((i) => (
                      <li key={i.role} className="stack-sm" style={{ gap: 2 }}>
                        <span className="small"><strong>{humanize(i.role)}</strong> <RefLink refId={i.ref} /></span>
                        <HashChip value={i.sha256} />
                      </li>
                    ))}
                  </ul>
                </Panel>
                <Panel title="Provenance">
                  <KeyValue compact items={[['Checker', <span key="c" className="small">{e.checker}</span>], ['Evidence hash', <HashChip key="h" value={e.evidenceSha256} />], ['Change', e.changeId ?? '—']]} />
                </Panel>
                <Applicability e={e} />
              </div>
            </div>
          </div>
        )}
      </QueryState>
    </div>
  );
}
