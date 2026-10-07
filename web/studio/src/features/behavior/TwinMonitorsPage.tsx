/**
 * Operate → Behaviour → Monitors: the live result of every monitor of this twin, the alerts its
 * alert policy raises now, and the requirements they check. Each monitor is evaluated by its
 * authority on the backend (GET /instances/{id}/monitors): conformance by the runtime, property
 * monitors on the kernel's committed state, data-quality monitors on stored telemetry. A
 * monitor that cannot be evaluated says why ("unknown"); nothing is inferred here.
 */
import { useQuery } from '@tanstack/react-query';
import { BellRing, ExternalLink, ShieldCheck } from 'lucide-react';
import { Link } from 'react-router-dom';
import { api } from '@/api/client';
import type { InstanceMonitorsView as MonitorsView } from '@/api/types';
import { Callout, EmptyState, PageHeader, Panel, QueryState, StatusBadge, TimeStamp } from '@/design';
import { useTwinScope } from '@/app/twinScope';

const STATUS: Record<string, { tone: 'ok' | 'critical' | 'warning' | 'neutral'; label: string }> = {
  satisfied: { tone: 'ok', label: 'SATISFIED' },
  violated: { tone: 'critical', label: 'VIOLATED' },
  finding: { tone: 'warning', label: 'FINDING' },
  inconclusive: { tone: 'warning', label: 'INCONCLUSIVE' },
  unknown: { tone: 'neutral', label: 'UNKNOWN' },
  error: { tone: 'critical', label: 'CHECK ERROR' },
  not_monitored: { tone: 'neutral', label: 'NOT MONITORED' },
};

const KIND: Record<string, string> = { conformance: 'Conformance', property: 'Property', data_quality: 'Data quality' };

export default function TwinMonitorsPage() {
  const scope = useTwinScope();
  const twinId = scope?.twin.id ?? '';
  const q = useQuery({
    queryKey: ['instances', 'monitors', twinId],
    queryFn: () => api.get<MonitorsView>(`/instances/${encodeURIComponent(twinId)}/monitors`),
    enabled: !!scope?.twin.blueprintId,
    refetchInterval: 5000,
    retry: false,
  });
  if (!scope) return <EmptyState title="Open a twin first">Monitors belong to a twin.</EmptyState>;
  const twin = scope.twin;
  const studio = twin.blueprintId ? `/studio/blueprints/${encodeURIComponent(twin.blueprintId)}/v/${twin.blueprintVersion ?? 1}/assurance/monitors` : null;
  return (
    <div className="vts-page stack">
      <PageHeader
        title="Monitors"
        meta={<span>What this twin checks continuously, evaluated live by the backend.</span>}
        actions={
          studio && (
            <Link to={studio} className="vts-btn vts-btn--secondary">
              <ExternalLink size={14} aria-hidden="true" /> Edit monitors in Studio
            </Link>
          )
        }
      />
      {!twin.blueprintId ? (
        <EmptyState title="No Blueprint monitors">This twin was registered without a Blueprint; its conformance is shown under Behaviour → Conformance.</EmptyState>
      ) : (
        <QueryState query={q}>
          {(v) => {
            const active = v.alerts.filter((a) => a.active);
            return (
              <div className="stack">
                {!v.runtime.available && (
                  <Callout tone="warning" title="Runtime not available">
                    {v.runtime.note}. Conformance and property monitors are UNKNOWN until the twin runs; data-quality monitors use stored telemetry.
                  </Callout>
                )}
                <Panel
                  title={
                    <span className="row">
                      <BellRing size={15} aria-hidden="true" /> Active alerts
                    </span>
                  }
                  actions={<StatusBadge tone={active.length ? 'critical' : 'ok'} label={active.length ? `${active.length} active` : 'None active'} />}
                >
                  {active.length === 0 ? (
                    <p className="small muted" style={{ margin: 0 }}>
                      No monitor result triggers the alert policy right now.
                    </p>
                  ) : (
                    <ul className="vts-findings">
                      {active.map((a) => (
                        <li key={a.id}>
                          <StatusBadge tone={a.severity === 'critical' ? 'critical' : a.severity === 'warning' ? 'warning' : 'info'} label={a.severity.toUpperCase()} />
                          <span>
                            <strong>{a.message}</strong> <span className="xsmall subtle">({a.monitor} is {a.monitorStatus})</span>
                          </span>
                        </li>
                      ))}
                    </ul>
                  )}
                </Panel>
                <Panel title="Monitors" subtitle={<>Evaluated <TimeStamp value={v.evaluatedAt} relative />{v.runtime.time ? ` · twin at t = ${v.runtime.time.text}` : ''}</>} flush>
                  <table className="vts-table">
                    <caption className="sr-only">Monitor results</caption>
                    <thead>
                      <tr>
                        <th scope="col">Monitor</th>
                        <th scope="col">Kind</th>
                        <th scope="col">Result</th>
                        <th scope="col">Detail</th>
                        <th scope="col">Evaluated by</th>
                      </tr>
                    </thead>
                    <tbody>
                      {v.monitors.map((m) => {
                        const s = STATUS[m.status] ?? STATUS.unknown!;
                        return (
                          <tr key={m.id}>
                            <td>
                              <strong className="small">{m.name}</strong> <span className="mono xsmall subtle">{m.id}</span>
                              {m.requirement && <div className="xsmall subtle">checks {m.requirement}</div>}
                            </td>
                            <td className="small">{KIND[m.kind] ?? m.kind}</td>
                            <td>
                              <StatusBadge tone={s.tone} label={s.label} />
                            </td>
                            <td className="xsmall muted" style={{ maxWidth: 460 }}>
                              {m.detail}
                            </td>
                            <td className="xsmall">{m.evaluator}</td>
                          </tr>
                        );
                      })}
                    </tbody>
                  </table>
                </Panel>
                <Panel
                  title={
                    <span className="row">
                      <ShieldCheck size={15} aria-hidden="true" /> Requirements
                    </span>
                  }
                  flush
                >
                  <table className="vts-table">
                    <caption className="sr-only">Requirement status</caption>
                    <tbody>
                      {v.requirements.map((r) => {
                        const s = STATUS[r.status] ?? STATUS.unknown!;
                        return (
                          <tr key={r.id}>
                            <td className="mono small">{r.id}</td>
                            <td className="small">{r.title}</td>
                            <td className="xsmall">{r.category}</td>
                            <td>
                              <StatusBadge tone={s.tone} label={s.label} />
                            </td>
                          </tr>
                        );
                      })}
                    </tbody>
                  </table>
                  <p className="xsmall subtle" style={{ padding: '8px 16px', margin: 0 }}>
                    A requirement is satisfied while every monitor that checks it is; a run-time monitor result is not a proof.
                  </p>
                </Panel>
              </div>
            );
          }}
        </QueryState>
      )}
    </div>
  );
}
