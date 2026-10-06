/**
 * Refinement evidence (Definition 4): verdict, the four conditions, every proof
 * obligation with counter-models, hashes, checker identity and assumptions.
 * CHECK FAILED (the checker could not run) is never shown as NOT A REFINEMENT.
 */
import { Download } from 'lucide-react';
import { useState } from 'react';
import { Link, useParams } from 'react-router-dom';
import { useEvidence } from '@/api/queries';
import type { RefinementDocument } from '@/api/types';
import { refinementDocumentSchema } from '@/api/schemas';
import {
  Button,
  Callout,
  ConditionBadge,
  Formula,
  HashChip,
  KeyValue,
  PageHeader,
  Panel,
  QueryState,
  RefinementBadge,
  Segmented,
  TimeStamp,
  downloadText,
} from '@/design';
import { ChangeLink, Crumbs, RefLink, LearnMore } from '@/features/common/links';

const EXPLAIN: Record<RefinementDocument['verdict'], string> = {
  valid_refinement:
    'Every condition of Definition 4 holds. If both interpretations were included, an existing alignment under the base domain knowledge carries over to the candidate (Theorem 3).',
  not_a_refinement:
    'At least one condition is violated. Existing alignment evidence cannot be carried over by Theorem 3: alignment must be re-established for the candidate.',
  unknown: 'No condition is violated, but the solver could not decide some obligations within the time limit. Nothing is concluded.',
  check_failed: 'The checker could not evaluate the definition (see the reasons). This says nothing about refinement.',
};

export default function RefinementPage() {
  const { evidenceId = '' } = useParams();
  const q = useEvidence(evidenceId);
  const [filter, setFilter] = useState<'problems' | 'all'>('problems');
  return (
    <div className="vts-page">
      <QueryState query={q}>
        {(e) => {
          const parsed = refinementDocumentSchema.safeParse(e.document);
          if (!parsed.success) {
            return <Callout tone="critical" title="Unreadable refinement evidence">The evidence document does not have the expected format, so no verdict is shown.</Callout>;
          }
          const d = e.document as unknown as RefinementDocument;
          const obligations = d.obligations.filter((o) => filter === 'all' || o.status !== 'holds');
          return (
            <div className="stack">
              <PageHeader
                eyebrow={<Crumbs items={[{ label: 'Studio', to: '/studio' }, { label: 'Changes', to: '/studio/changes' }, { label: 'Refinement' }, { label: e.id }]} />}
                title={<span className="row-wrap">Refinement check {e.id} <RefinementBadge verdict={d.verdict} size="lg" /></span>}
                meta={
                  <>
                    <span><RefLink refId={d.candidate.ontology} kind="ontology" /> ⊑ <RefLink refId={d.base.ontology} kind="ontology" /> ?</span>
                    <TimeStamp value={e.createdAt} kind="evidence" showKind />
                    <span>by {e.createdBy}</span>
                    {e.changeId && <span>in <ChangeLink id={e.changeId} /></span>}
                    <LearnMore page="refinement.html#understanding-the-four-results">Understanding refinement results</LearnMore>
                  </>
                }
                actions={
                  <Button icon={<Download size={14} />} onClick={() => downloadText(`${e.id}.json`, 'application/json', JSON.stringify(e, null, 2))}>
                    Export evidence (JSON)
                  </Button>
                }
              />
              <Callout tone={d.verdict === 'valid_refinement' ? 'ok' : d.verdict === 'unknown' ? 'warning' : 'critical'} title={d.summary}>
                {EXPLAIN[d.verdict]}
              </Callout>
              {d.failureReasons.length > 0 && (
                <Callout tone="critical" title="Why the check could not run">
                  <ul style={{ margin: 0, paddingLeft: 16 }}>{d.failureReasons.map((r) => <li key={r}>{r}</li>)}</ul>
                </Callout>
              )}
              <div className="grid-main-side">
                <div className="stack">
                  <Panel title="Conditions of Definition 4" subtitle="Φ₁ = candidate, Φ₂ = base" flush>
                    <table className="vts-table">
                      <caption className="sr-only">Conditions</caption>
                      <tbody>
                        {d.conditions.map((c) => (
                          <tr key={c.condition}>
                            <td className="strong mono" style={{ width: 50 }}>({c.condition})</td>
                            <td>
                              <div className="small">{c.title}</div>
                              {c.details.map((x) => <div key={x} className="xsmall subtle">{x}</div>)}
                            </td>
                            <td style={{ width: 140 }}><ConditionBadge status={c.status} /></td>
                          </tr>
                        ))}
                      </tbody>
                    </table>
                  </Panel>
                  <Panel
                    title={`Proof obligations (${d.obligations.length})`}
                    subtitle="Each discharged by Z3 over the aligner's reading of the formulas"
                    actions={<Segmented label="Show" value={filter} onChange={setFilter} options={[{ id: 'problems', label: 'Problems' }, { id: 'all', label: 'All' }]} />}
                    flush
                  >
                    {obligations.length === 0 ? (
                      <p className="small muted" style={{ padding: 16 }}>No violated or undecided obligation.</p>
                    ) : (
                      <ul className="vts-list" style={{ padding: '0 var(--s-4)' }}>
                        {obligations.map((o, i) => (
                          <li key={i} className="stack-sm">
                            <div className="row-between">
                              <span className="small"><span className="mono strong">({o.condition})</span> {o.subject}</span>
                              <ConditionBadge status={o.status} />
                            </div>
                            <Formula>{o.statement}</Formula>
                            {o.note && <span className="small">{o.note}</span>}
                            {o.counterModel.length > 0 && (
                              <details open={o.status === 'violated'}>
                                <summary className="small">Counter-model (a model of the candidate axioms violating the obligation)</summary>
                                <table className="vts-table">
                                  <caption className="sr-only">Counter-model</caption>
                                  <tbody>
                                    {o.counterModel.map((m) => <tr key={m.symbol}><td className="mono small">{m.symbol}</td><td className="mono small">{m.value}</td></tr>)}
                                  </tbody>
                                </table>
                              </details>
                            )}
                          </li>
                        ))}
                      </ul>
                    )}
                  </Panel>
                </div>
                <div className="stack">
                  <Panel title="Domain knowledge compared">
                    <KeyValue
                      compact
                      items={[
                        ['Base ontology', <RefLink key="b" refId={d.base.ontology} kind="ontology" />],
                        ['Base hash', <HashChip key="bh" value={d.hashes.baseOntology} />],
                        ['Candidate ontology', <RefLink key="c" refId={d.candidate.ontology} kind="ontology" />],
                        ['Candidate hash', <HashChip key="ch" value={d.hashes.candidateOntology} />],
                        ['PT interpretation', d.base.ptInterpretation ? <span key="p"><RefLink refId={d.base.ptInterpretation} kind="interpretation" /> → {d.candidate.ptInterpretation ? <RefLink refId={d.candidate.ptInterpretation} kind="interpretation" /> : '—'}</span> : 'not part of the check'],
                        ['DT interpretation', d.base.dtInterpretation ? <span key="d"><RefLink refId={d.base.dtInterpretation} kind="interpretation" /> → {d.candidate.dtInterpretation ? <RefLink refId={d.candidate.dtInterpretation} kind="interpretation" /> : '—'}</span> : 'not part of the check'],
                      ]}
                    />
                  </Panel>
                  <Panel title="Checker">
                    <KeyValue
                      compact
                      items={[
                        ['Definition', d.definition ?? 'Def. 4'],
                        ['Checker', <span key="c" className="small">{d.checker}</span>],
                        ['Per-query timeout', `${d.timeoutMs} ms`],
                        ['Executed', <TimeStamp key="t" value={e.createdAt} />],
                        ['Evidence hash', <HashChip key="h" value={e.evidenceSha256} />],
                      ]}
                    />
                  </Panel>
                  <Panel title="Assumptions">
                    <ul className="small" style={{ margin: 0, paddingLeft: 16 }}>{d.assumptions.map((a) => <li key={a}>{a}</li>)}</ul>
                  </Panel>
                  <Link className="small" to={`/studio/impact?ref=${encodeURIComponent(d.candidate.ontology)}`}>Impact of {d.candidate.ontology}</Link>
                </div>
              </div>
            </div>
          );
        }}
      </QueryState>
    </div>
  );
}
