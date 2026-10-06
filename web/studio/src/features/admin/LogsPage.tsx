/**
 * Administration › System logs: structured diagnostic logs of twin-studio.
 * Diagnostics only — not evidence. Secrets are redacted server-side.
 */
import { useState } from 'react';
import { useSearchParams } from 'react-router-dom';
import { useLogs } from '@/api/queries';
import type { LogEntry } from '@/api/types';
import { Button, Drawer, KeyValue, PageHeader, Panel, QueryState, Segmented, StatusBadge, TimeStamp } from '@/design';

/** `datetime-local` value (viewer's time zone) → ISO 8601 UTC, as the API expects. */
const toIso = (local: string | null) => {
  if (!local) return undefined;
  const t = new Date(local);
  return Number.isNaN(t.getTime()) ? undefined : t.toISOString();
};

const LEVEL_TONE: Record<LogEntry['level'], 'neutral' | 'info' | 'warning' | 'critical'> = { debug: 'neutral', info: 'info', warn: 'warning', error: 'critical' };

export default function LogsPage() {
  const [params, setParams] = useSearchParams();
  const level = params.get('level') ?? 'info';
  const [live, setLive] = useState(true);
  const [selected, setSelected] = useState<LogEntry | null>(null);
  const filters = {
    level,
    component: params.get('component') ?? undefined,
    q: params.get('q') ?? undefined,
    correlation: params.get('correlation') ?? undefined,
    execution: params.get('execution') ?? undefined,
    asset: params.get('asset') ?? undefined,
    since: toIso(params.get('from')),
    until: toIso(params.get('to')),
    limit: 300,
  };
  const q = useLogs(filters, live);
  const set = (k: string, v: string) => {
    const p = new URLSearchParams(params);
    if (v) p.set(k, v);
    else p.delete(k);
    setParams(p, { replace: true });
  };
  return (
    <div className="vts-page">
      <PageHeader
        eyebrow="Administration"
        title="System logs"
        meta={<span>Diagnostic application logs (not formal evidence). Secrets are redacted before they are written.</span>}
        actions={<Button size="sm" onClick={() => setLive((l) => !l)}>{live ? 'Pause auto-refresh' : 'Resume auto-refresh'}</Button>}
      />
      <Panel
        title="Entries"
        actions={
          <>
            <Segmented label="Minimum level" value={level} onChange={(v) => set('level', v)} options={[{ id: 'debug', label: 'Debug' }, { id: 'info', label: 'Info' }, { id: 'warn', label: 'Warn' }, { id: 'error', label: 'Error' }]} />
            <input className="vts-input" placeholder="Component (prefix)" defaultValue={filters.component} onBlur={(e) => set('component', e.target.value)} aria-label="Component" />
            <input className="vts-input" placeholder="Message contains…" defaultValue={filters.q} onBlur={(e) => set('q', e.target.value)} aria-label="Message search" />
            <input className="vts-input" placeholder="Correlation id" defaultValue={filters.correlation} onBlur={(e) => set('correlation', e.target.value)} aria-label="Correlation id" />
            <input className="vts-input" placeholder="Asset id" defaultValue={filters.asset} onBlur={(e) => set('asset', e.target.value)} aria-label="Asset id" />
            <input className="vts-input" placeholder="Execution id" defaultValue={filters.execution} onBlur={(e) => set('execution', e.target.value)} aria-label="Execution id" />
            <input className="vts-input" type="datetime-local" step={1} defaultValue={params.get('from') ?? ''} onBlur={(e) => set('from', e.target.value)} aria-label="From (local time)" />
            <input className="vts-input" type="datetime-local" step={1} defaultValue={params.get('to') ?? ''} onBlur={(e) => set('to', e.target.value)} aria-label="To (local time)" />
          </>
        }
        flush
      >
        <QueryState query={q}>
          {(d) => (
            <div className="vts-table-wrap" style={{ maxHeight: 640 }}>
              <table className="vts-table">
                <caption className="sr-only">Log entries (newest first)</caption>
                <thead><tr><th scope="col">Time</th><th scope="col">Level</th><th scope="col">Component</th><th scope="col">Message</th><th scope="col">Correlation</th></tr></thead>
                <tbody>
                  {d.items.map((e, i) => (
                    <tr key={`${e.ts}-${i}`} className="is-clickable" onClick={() => setSelected(e)} tabIndex={0} onKeyDown={(k) => k.key === 'Enter' && setSelected(e)}>
                      <td className="nowrap small"><TimeStamp value={e.ts} /></td>
                      <td><StatusBadge tone={LEVEL_TONE[e.level]} label={e.level} /></td>
                      <td className="mono small">{e.component}</td>
                      <td className="small">{e.message}</td>
                      <td className="mono xsmall">{e.correlationId ?? ''}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          )}
        </QueryState>
      </Panel>
      <Drawer open={!!selected} onOpenChange={(o) => !o && setSelected(null)} title="Log entry" subtitle={selected?.component}>
        {selected && (
          <div className="stack">
            <KeyValue compact items={[['Time', <TimeStamp key="t" value={selected.ts} />], ['Level', selected.level], ['Message', selected.message], ['Correlation', selected.correlationId ?? '—'], ['Execution', selected.executionId ?? '—'], ['Asset', selected.assetId ?? '—']]} />
            <pre className="vts-code">{JSON.stringify(selected.fields, null, 2)}</pre>
          </div>
        )}
      </Drawer>
    </div>
  );
}
