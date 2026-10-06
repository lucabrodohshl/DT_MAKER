/**
 * Conformance of the physical behaviour to the verified model.
 *
 * The status is the runtime's own conformance computation (from kernel verdicts
 * only). This view distinguishes, using the kernel's error codes:
 *   - model violation        (transition_not_enabled, invariant_violation, incompatible_observation)
 *   - delayed observation    (time_regression)
 *   - monitoring alarm       (deadline missed)
 *   - infrastructure error   (unavailable, io_error, internal)
 *   - insufficient data      (runtime not connected / no observation yet) — never a deviation.
 */
import { AlertOctagon, CheckCircle2, CircleHelp, PlugZap } from 'lucide-react';
import { Link } from 'react-router-dom';
import { ApiError } from '@/api/client';
import type { TwinDetail } from '@/api/types';
import { useLedger, useRuntimeState } from '@/runtime/client';
import type { LedgerRecord } from '@/runtime/types';
import { Callout, EmptyState, KeyValue, LogicalTimeText, Panel, StatusBadge } from '@/design';

const CATEGORY: Record<string, string> = {
  transition_not_enabled: 'Model violation',
  invariant_violation: 'Model violation (timing)',
  incompatible_observation: 'Model violation (observation)',
  time_regression: 'Delayed / out-of-order observation',
  time_not_representable: 'Malformed observation time',
  unavailable: 'Infrastructure error',
  io_error: 'Infrastructure error',
  internal: 'Infrastructure error',
};

function errorOf(r: LedgerRecord): { code: string; message: string } | null {
  const e = r.body.outcome?.error ?? (r.body as { error?: { code: string; message: string } }).error;
  return e ? { code: e.code, message: e.message } : null;
}

export function ConformanceView({ twin, connected }: { twin: TwinDetail; connected: boolean }) {
  const state = useRuntimeState(connected ? twin.id : null);
  const rejects = useLedger(connected ? twin.id : null, '', 0, 'reject', 50);
  const alarms = useLedger(connected ? twin.id : null, '', 0, 'alarm', 50);
  if (!connected || (state.error instanceof ApiError && state.error.isRuntimeNotConnected)) {
    return (
      <Panel title="Conformance">
        <EmptyState icon={<PlugZap size={28} />} title="Unknown — insufficient data">
          Conformance is decided by the runtime from the kernel's verdicts on observations. No runtime is connected, so no
          verdict exists. This is not reported as a deviation.
        </EmptyState>
      </Panel>
    );
  }
  const c = state.data?.conformance;
  if (!state.data || !c) {
    return <Panel title="Conformance"><p className="small muted">{state.isError ? (state.error as Error).message : 'Loading…'}</p></Panel>;
  }
  const noData = c.observations === 0 && c.decisions === 0;
  const conformant = c.status === 'conformant';
  const unit = twin.presentation.timeUnit;
  const records = [...(rejects.data?.records ?? []), ...(alarms.data?.records ?? [])].sort((a, b) => b.seq - a.seq);
  return (
    <div className="grid-main-side">
      <div className="stack">
        <Panel title="Conformance status" subtitle="Physical behaviour against the verified DT view">
          <div className="stack">
            {noData ? (
              <StatusBadge tone="warning" icon={CircleHelp} label="Unknown — no observation received yet" size="lg" />
            ) : conformant ? (
              <StatusBadge tone="ok" icon={CheckCircle2} label="Conformant" size="lg" />
            ) : (
              <StatusBadge tone="critical" icon={AlertOctagon} label="Deviation detected" size="lg" />
            )}
            {!conformant && c.first_violation && (
              <Callout tone="critical" title="First divergence">
                {c.first_violation}
                {c.first_violation_seq !== null && (
                  <>
                    {' '}
                    — ledger record{' '}
                    <Link to={`/audit/ledger?twin=${encodeURIComponent(twin.id)}&session=${encodeURIComponent(state.data.session)}&seq=${c.first_violation_seq}`}>
                      #{c.first_violation_seq}
                    </Link>
                  </>
                )}
              </Callout>
            )}
            {!state.data.deterministic && (
              <Callout tone="info" title="Ambiguous state">
                The observations are consistent with several model states. This is reported as ambiguity, not as a deviation.
              </Callout>
            )}
            <KeyValue
              items={[
                ['Observations judged', `${c.observations} (${c.observations_rejected} refused)`],
                ['Decisions judged', `${c.decisions} (${c.decisions_rejected} refused)`],
                ['Monitoring alarms', String(c.alarms)],
                ['Current model state', state.data.configurations.map((x) => twin.presentation.states?.[x.location]?.label ?? x.location).join(', ')],
                ['Logical time', <LogicalTimeText key="t" text={state.data.time.text} unit={unit} />],
                ['Execution', <span key="s" className="mono small">{state.data.session}</span>],
                ['Model', state.data.model ? `${state.data.model.id} ${state.data.model.version}` : '—'],
              ]}
            />
            <p className="xsmall subtle">{c.definition}</p>
          </div>
        </Panel>
      </div>
      <Panel title="Refusals and alarms" subtitle="Kernel verdicts recorded in the ledger" flush>
        {records.length === 0 ? (
          <EmptyState compact title="None">The kernel has not refused any input and no alarm was raised.</EmptyState>
        ) : (
          <ul className="vts-list" style={{ padding: '0 var(--s-4)' }}>
            {records.map((r) => {
              const e = errorOf(r);
              const cat = r.body.kind === 'alarm' ? 'Monitoring alarm' : e ? CATEGORY[e.code] ?? 'Refused input' : 'Refused input';
              return (
                <li key={r.seq} className="stack-sm" style={{ gap: 2 }}>
                  <span className="row-between">
                    <StatusBadge tone={cat.startsWith('Infrastructure') ? 'warning' : 'critical'} label={cat} />
                    <span className="xsmall subtle">#{r.seq}</span>
                  </span>
                  <span className="small">
                    {r.body.input ? <span className="mono">{r.body.input.name}</span> : null} {e?.message ?? String((r.body as { alarm?: string }).alarm ?? '')}
                  </span>
                  <span className="xsmall subtle">
                    {r.body.input?.source ? `from ${r.body.input.source} · ` : ''}code {e?.code ?? r.body.kind}
                  </span>
                </li>
              );
            })}
          </ul>
        )}
      </Panel>
    </div>
  );
}
