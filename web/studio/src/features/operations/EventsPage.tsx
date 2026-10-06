/**
 * Operations › Events & alerts.
 *
 * Two timelines, because their clocks differ and are never mixed silently:
 *  - operational events of each twin (kernel transitions, refusals, alarms,
 *    planner/command context) ordered by LOGICAL time, from the runtime ledger;
 *  - engineering, deployment and data-quality events ordered by WALL-CLOCK time.
 * Consecutive identical events are grouped to reduce noise.
 */
import { Link, useSearchParams } from 'react-router-dom';
import { useAudit, useOverview, useTwins } from '@/api/queries';
import type { TwinSummary } from '@/api/types';
import { useLedger } from '@/runtime/client';
import type { LedgerRecord } from '@/runtime/types';
import { ticksToText } from '@/runtime/time';
import { EmptyState, FreshnessBadge, PageHeader, Panel, QueryState, RuntimeUnavailable, Segmented, StatusBadge, TimeStamp } from '@/design';
import { AuditOperation } from '@/features/audit/auditFormat';
import { KIND_LABEL, kindTone, recordSummary } from '@/features/audit/ledger';
import { useTwinScope } from '@/app/twinScope';

type Severity = 'all' | 'warning' | 'critical';

function severityOf(r: LedgerRecord): 'info' | 'warning' | 'critical' {
  if (r.body.kind === 'reject') return 'critical';
  if (r.body.kind === 'alarm') return 'warning';
  return 'info';
}

/** Group consecutive records with the same summary. */
export function group<T>(items: T[], key: (t: T) => string): { item: T; count: number }[] {
  const out: { item: T; count: number }[] = [];
  for (const it of items) {
    const last = out[out.length - 1];
    if (last && key(last.item) === key(it)) last.count++;
    else out.push({ item: it, count: 1 });
  }
  return out;
}

function TwinEvents({ twin, severity, text }: { twin: TwinSummary; severity: Severity; text: string }) {
  const ledger = useLedger(twin.runtimeUrl ? twin.id : null, '', 0, '', 200);
  if (!twin.runtimeUrl) return <Panel title={twin.name}><RuntimeUnavailable compact /></Panel>;
  return (
    <Panel title={`${twin.name} — operational events`} subtitle="Ordered by logical model time (current execution)" flush>
      <QueryState query={ledger} compact>
        {(page) => {
          const records = [...page.records]
            .reverse()
            .filter((r) => r.body.kind !== 'genesis')
            .filter((r) => severity === 'all' || (severity === 'critical' ? severityOf(r) === 'critical' : severityOf(r) !== 'info'))
            .filter((r) => !text || recordSummary(r).toLowerCase().includes(text.toLowerCase()));
          const grouped = group(records, (r) => `${r.body.kind}|${recordSummary(r)}`);
          if (grouped.length === 0) return <EmptyState compact title="No matching events" />;
          return (
            <ul className="vts-list" style={{ padding: '0 var(--s-4)', maxHeight: 520, overflow: 'auto' }}>
              {grouped.map(({ item: r, count }) => (
                <li key={r.seq} className="row-between">
                  <span className="row-wrap small" style={{ minWidth: 0 }}>
                    <StatusBadge tone={kindTone(r.body.kind)} label={KIND_LABEL[r.body.kind] ?? r.body.kind} />
                    <span>{recordSummary(r)}</span>
                    {count > 1 && <span className="vts-tag">×{count}</span>}
                  </span>
                  <span className="xsmall subtle nowrap">
                    t = {ticksToText(r.body.time_after ?? r.body.at ?? 0, r.body.time_base ?? twin.ticksPerUnit ?? 1000)} {twin.presentation.timeUnit ?? ''} ·{' '}
                    <Link to={`/audit/provenance?twin=${encodeURIComponent(twin.id)}&session=${encodeURIComponent(r.body.session)}&seq=${r.seq}`}>#{r.seq}</Link>
                  </span>
                </li>
              ))}
            </ul>
          );
        }}
      </QueryState>
    </Panel>
  );
}

export default function EventsPage() {
  const [params, setParams] = useSearchParams();
  const severity = (params.get('severity') as Severity) || 'all';
  const text = params.get('q') ?? '';
  const scope = useTwinScope();
  const twinFilter = scope?.twin.id ?? params.get('twin') ?? '';
  const twins = useTwins();
  const overview = useOverview();
  const audit = useAudit({ limit: 50 });
  const set = (k: string, v: string) => {
    const p = new URLSearchParams(params);
    if (v) p.set(k, v);
    else p.delete(k);
    setParams(p, { replace: true });
  };
  return (
    <div className="vts-page">
      <PageHeader
        eyebrow="Operations"
        title="Events & alerts"
        actions={
          <>
            <input className="vts-input" placeholder="Search events…" value={text} onChange={(e) => set('q', e.target.value)} aria-label="Search events" />
            {!scope && <select className="vts-select" value={twinFilter} onChange={(e) => set('twin', e.target.value)} aria-label="Twin">
              <option value="">All twins</option>
              {twins.data?.map((t) => <option key={t.id} value={t.id}>{t.name}</option>)}
            </select>}
            <Segmented label="Severity" value={severity} onChange={(v) => set('severity', v === 'all' ? '' : v)} options={[{ id: 'all', label: 'All' }, { id: 'warning', label: 'Warnings+' }, { id: 'critical', label: 'Critical' }]} />
          </>
        }
      />
      <div className="grid-main-side">
        <div className="stack">
          {twins.data?.filter((t) => !twinFilter || t.id === twinFilter).map((t) => <TwinEvents key={t.id} twin={t} severity={severity} text={text} />)}
        </div>
        <div className="stack">
          <Panel title="Data-quality alerts" subtitle="Telemetry sources that are stale, missing or invalid (wall clock)">
            <QueryState query={overview} compact>
              {(o) =>
                o.telemetry.attention.length === 0 ? (
                  <p className="small muted">All sources are fresh.</p>
                ) : (
                  <ul className="vts-list">
                    {o.telemetry.attention.map((a) => (
                      <li key={a.channelId} className="row-between small">
                        <Link to={`/assets/${a.assetId}/telemetry?channel=${encodeURIComponent(a.channelId)}`}>{a.label}</Link>
                        <span className="row"><FreshnessBadge freshness={a.freshness} />{a.lastObservedAt && <TimeStamp value={a.lastObservedAt} relative />}</span>
                      </li>
                    ))}
                  </ul>
                )
              }
            </QueryState>
          </Panel>
          <Panel title="Engineering & deployment events" subtitle="Wall-clock time" flush>
            <QueryState query={audit} compact>
              {(d) => (
                <ul className="vts-list" style={{ padding: '0 var(--s-4)' }}>
                  {group(d.items.filter((r) => !text || `${r.operation} ${r.subject}`.toLowerCase().includes(text.toLowerCase())), (r) => `${r.operation}|${r.subject}|${r.outcome}`).map(({ item, count }) => (
                    <li key={item.seq} className="stack-sm" style={{ gap: 2 }}>
                      <span className="row-wrap"><AuditOperation record={item} />{count > 1 && <span className="vts-tag">×{count}</span>}</span>
                      <span className="xsmall subtle"><TimeStamp value={item.at} /></span>
                    </li>
                  ))}
                </ul>
              )}
            </QueryState>
          </Panel>
        </div>
      </div>
    </div>
  );
}
