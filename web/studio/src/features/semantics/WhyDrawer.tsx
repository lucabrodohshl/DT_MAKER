/**
 * "Why?" — the evidence chain behind a state or event of a twin:
 *
 *   behavioural state (kernel) → proposition → interpretation formula (I_D)
 *   → truth under the current observations (Z3, three-valued)
 *   → contributing observations (channel, value, observation time, quality, source)
 *   → ontology axioms constraining the symbols involved
 *   → ontology / interpretation versions and hashes → behavioural effect.
 *
 * Every element is fetched from the backend; nothing is paraphrased or guessed.
 */
import { useQueries } from '@tanstack/react-query';
import { Link } from 'react-router-dom';
import { api } from '@/api/client';
import type { EvaluationResult, SymbolInfo, TwinDetail } from '@/api/types';
import type { RuntimeState } from '@/runtime/types';
import {
  Callout,
  Drawer,
  FreshnessBadge,
  Formula,
  HashChip,
  KeyValue,
  StatusBadge,
  TimeStamp,
  TruthBadge,
  displayUnit,
} from '@/design';
import { AssetLink, RefLink } from '@/features/common/links';
import { useSemanticFacts } from './useSemanticFacts';

function Step({ n, title, children }: { n: number; title: string; children: React.ReactNode }) {
  return (
    <li className="vts-chain__item">
      <span className="vts-chain__marker" aria-hidden="true" />
      <div className="stack-sm">
        <span className="xsmall subtle strong">
          {n}. {title}
        </span>
        <div>{children}</div>
      </div>
    </li>
  );
}

export function WhyDrawer({
  twin,
  entryKey,
  state,
  open,
  onOpenChange,
}: {
  twin: TwinDetail;
  entryKey: string | null;
  state: RuntimeState | null;
  open: boolean;
  onOpenChange: (o: boolean) => void;
}) {
  const facts = useSemanticFacts(twin, { refetchMs: 20_000 });
  const entry = facts.data?.entries.find((e) => e.key === entryKey);
  const ontologyRef = facts.data?.ontologyRef ?? twin.bindings.find((b) => b.role === 'ontology')?.ref;
  const symbols = entry?.symbols ?? [];
  const symbolInfos = useQueries({
    queries: symbols.map((s) => ({
      queryKey: ['artifacts', 'symbol', ontologyRef, s],
      queryFn: () => {
        const [id, v] = ontologyRef!.split('@');
        return api.get<SymbolInfo>(`/artifacts/${encodeURIComponent(id!)}/versions/${v}/symbols/${encodeURIComponent(s)}`);
      },
      enabled: open && !!ontologyRef,
    })),
  });
  const isEvent = entryKey?.endsWith('!') ?? false;
  const label = isEvent ? twin.presentation.events?.[entryKey!.slice(0, -1)]?.label : twin.presentation.states?.[entryKey ?? '']?.label;
  const current = state?.configurations.map((c) => c.location) ?? [];
  const relevantObs = (facts.data?.observations ?? []).filter((o) => symbols.includes(o.symbol));
  const axioms = new Map<string, { id: string; formula: string }>();
  symbolInfos.forEach((q) => q.data?.axioms.forEach((a) => axioms.set(a.id, a)));
  const effects = state?.enabled.filter((t) => current.includes(t.source)) ?? [];

  return (
    <Drawer
      open={open}
      onOpenChange={onOpenChange}
      title={`Why: ${label ?? entryKey ?? ''}`}
      subtitle={isEvent ? 'Meaning of an event and the observations that support it' : 'Meaning of a behavioural state and the observations that support it'}
    >
      {!entryKey ? null : facts.isError ? (
        <Callout tone="critical">The interpretation could not be evaluated: {(facts.error as Error).message}</Callout>
      ) : !facts.data || !entry ? (
        <p className="small muted">Evaluating…</p>
      ) : (
        <ol className="vts-chain" style={{ listStyle: 'none', margin: 0, padding: 0 }}>
          {!isEvent && (
            <Step n={1} title="Behavioural state (verified kernel)">
              {state ? (
                current.includes(entryKey) ? (
                  <StatusBadge tone="info" label={`The twin is currently in ${entryKey}`} />
                ) : (
                  <span className="small">The twin is currently in {current.join(', ') || '—'}, not in {entryKey}.</span>
                )
              ) : (
                <span className="small muted">Runtime not connected: the current state is unknown to Studio.</span>
              )}
            </Step>
          )}
          <Step n={isEvent ? 1 : 2} title={isEvent ? 'Event label' : 'Observable proposition'}>
            <span className="mono small">{isEvent ? entryKey : `at(${entryKey})`}</span>
          </Step>
          <Step n={isEvent ? 2 : 3} title="Interpretation (domain meaning)">
            <div className="stack-sm">
              <Formula>{entry.formula}</Formula>
              <span className="xsmall subtle">
                From <RefLink refId={facts.data.interpretationRef} kind="interpretation" /> over{' '}
                <RefLink refId={facts.data.ontologyRef} kind="ontology" />
              </span>
            </div>
          </Step>
          <Step n={isEvent ? 3 : 4} title="Truth under the current observations">
            <div className="stack-sm">
              <TruthBadge truth={entry.truth} />
              {entry.truth === 'unknown' && entry.unobserved.length > 0 && (
                <span className="small">
                  Not decided by the data: no observation for {entry.unobserved.map((u) => <code key={u} className="vts-formula" style={{ marginRight: 4 }}>{u}</code>)}
                  (constants are fixed by the ontology axioms below).
                </span>
              )}
              {entry.solverNote && <span className="xsmall subtle">Solver: {entry.solverNote}</span>}
              <span className="xsmall subtle">Decided by {facts.data.checker} at <TimeStamp value={facts.data.evaluatedAt} /></span>
            </div>
          </Step>
          <Step n={isEvent ? 4 : 5} title="Contributing observations">
            {relevantObs.length === 0 ? (
              <span className="small muted">No telemetry channel provides the symbols used by this formula.</span>
            ) : (
              <table className="vts-table">
                <caption className="sr-only">Observations used</caption>
                <thead>
                  <tr>
                    <th scope="col">Symbol</th>
                    <th scope="col" className="num">Value</th>
                    <th scope="col">Observed</th>
                    <th scope="col">Freshness</th>
                  </tr>
                </thead>
                <tbody>
                  {relevantObs.map((o) => (
                    <tr key={o.symbol}>
                      <td>
                        <span className="mono small">{o.symbol}</span>
                        {o.channelId && o.assetId && (
                          <div className="xsmall">
                            <Link to={`/assets/${encodeURIComponent(o.assetId)}/telemetry?channel=${encodeURIComponent(o.channelId)}`}>{o.channelId}</Link> on <AssetLink id={o.assetId} />
                          </div>
                        )}
                      </td>
                      <td className="num">
                        {o.value} {displayUnit(o.unit)}
                      </td>
                      <td>
                        <TimeStamp value={o.observedAt} />
                        <div className="xsmall subtle">{o.source}</div>
                      </td>
                      <td>{o.freshness && <FreshnessBadge freshness={o.freshness} />}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            )}
          </Step>
          <Step n={isEvent ? 5 : 6} title="Thresholds and constraints (ontology axioms)">
            {axioms.size === 0 ? (
              <span className="small muted">No axiom constrains these symbols.</span>
            ) : (
              <ul className="vts-list">
                {[...axioms.values()].map((a) => (
                  <li key={a.id} className="stack-sm" style={{ gap: 2 }}>
                    <span className="small strong mono">{a.id}</span>
                    <Formula>{a.formula}</Formula>
                  </li>
                ))}
              </ul>
            )}
          </Step>
          <Step n={isEvent ? 6 : 7} title="Artefact provenance">
            <KeyValue
              compact
              items={[
                ['Ontology', <RefLink key="o" refId={facts.data.ontologyRef} kind="ontology" />],
                ['Ontology hash', <HashChip key="oh" value={facts.data.ontologySha256} />],
                ['Interpretation', <RefLink key="i" refId={facts.data.interpretationRef} kind="interpretation" />],
                ['Interpretation hash', <HashChip key="ih" value={facts.data.interpretationSha256} />],
              ]}
            />
          </Step>
          {!isEvent && (
            <Step n={8} title="Behavioural effect (kernel)">
              {!state ? (
                <span className="small muted">Runtime not connected.</span>
              ) : !current.includes(entryKey) ? (
                <span className="small muted">Not the current state, so its outgoing transitions are not being considered.</span>
              ) : effects.length === 0 ? (
                <span className="small">No outgoing transition from this state.</span>
              ) : (
                <ul className="vts-list">
                  {effects.map((t) => (
                    <li key={t.transition} className="row-between small">
                      <span>
                        <span className="mono">{t.label}</span> → {twin.presentation.states?.[t.target]?.label ?? t.target}
                      </span>
                      <StatusBadge tone={t.enabled_now ? 'ok' : 'info'} label={t.enabled_now ? 'Admissible now' : `Admissible from t = ${t.window.earliest.text}`} />
                    </li>
                  ))}
                </ul>
              )}
            </Step>
          )}
        </ol>
      )}
      {facts.data && facts.data.warnings.length > 0 && (
        <Callout tone="warning" title="Data quality">
          {facts.data.warnings.join(' ')}
        </Callout>
      )}
    </Drawer>
  );
}

export type { EvaluationResult };
