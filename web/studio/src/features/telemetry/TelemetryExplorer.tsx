/**
 * Telemetry explorer for one asset subtree: channel list with freshness, time
 * range control, charts grouped by unit, live follow mode and export.
 *
 * Time shown is observation (event) time in the viewer's time zone; ingestion
 * time is shown per sample in the table/tooltip. Stale data is labelled stale.
 */
import { useQueries } from '@tanstack/react-query';
import { Download, Pause, Play } from 'lucide-react';
import { useEffect, useMemo, useState } from 'react';
import { useSearchParams } from 'react-router-dom';
import { api } from '@/api/client';
import { keys, useChannels } from '@/api/queries';
import type { TelemetryChannel, TelemetrySeries } from '@/api/types';
import {
  Button,
  Callout,
  EmptyState,
  FreshnessBadge,
  Panel,
  QueryState,
  Segmented,
  TimeStamp,
  displayUnit,
  downloadText,
  formatValue,
} from '@/design';
import { TimeSeriesChart } from './TimeSeriesChart';

type Range = 'live' | '15m' | '1h' | '6h' | '24h' | '7d' | 'custom';
const RANGE_MS: Record<Exclude<Range, 'custom'>, number> = {
  live: 10 * 60_000,
  '15m': 15 * 60_000,
  '1h': 3_600_000,
  '6h': 6 * 3_600_000,
  '24h': 86_400_000,
  '7d': 7 * 86_400_000,
};
const COLORS = ['--series-1', '--series-2', '--series-3', '--series-4', '--series-5', '--series-6'];

function toLocalInput(ms: number): string {
  const d = new Date(ms);
  const pad = (n: number) => String(n).padStart(2, '0');
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}T${pad(d.getHours())}:${pad(d.getMinutes())}`;
}


export function exportCsv(series: TelemetrySeries[]): string {
  const lines = ['channel,unit,observed_at,ingested_at,value,quality,min,max,count'];
  for (const s of series) {
    if (s.downsampled && s.buckets) {
      for (const b of s.buckets) {
        lines.push([s.channelId, s.channel.unit, new Date(b.startMs).toISOString(), '', b.avg, b.bad ? 'bad' : b.uncertain ? 'uncertain' : 'good', b.min, b.max, b.count].join(','));
      }
    } else {
      for (const x of s.samples ?? []) {
        lines.push([s.channelId, s.channel.unit, x.observedAt, x.ingestedAt, JSON.stringify(x.value), x.quality, '', '', 1].join(','));
      }
    }
  }
  return lines.join('\n');
}

export function TelemetryExplorer({ assetId }: { assetId: string }) {
  const [params, setParams] = useSearchParams();
  const channels = useChannels(assetId, { refetchMs: 10_000 });
  const [range, setRange] = useState<Range>((params.get('range') as Range) || '1h');
  const [following, setFollowing] = useState(true);
  const [now, setNow] = useState(() => Date.now());
  const [customFrom, setCustomFrom] = useState(() => toLocalInput(Date.now() - 86_400_000));
  const [customTo, setCustomTo] = useState(() => toLocalInput(Date.now()));

  const selected = useMemo(() => {
    const fromUrl = params.getAll('channel');
    if (fromUrl.length) return fromUrl;
    const list = channels.data?.channels ?? [];
    return list.filter((c) => c.valueType === 'number').slice(0, 2).map((c) => c.id);
  }, [params, channels.data]);

  // Live follow: advance the window periodically (never while paused).
  useEffect(() => {
    if (!following || range === 'custom') return;
    const id = setInterval(() => setNow(Date.now()), range === 'live' ? 5000 : 30_000);
    return () => clearInterval(id);
  }, [following, range]);

  const [from, to] =
    range === 'custom' ? [new Date(customFrom).getTime(), new Date(customTo).getTime()] : [now - RANGE_MS[range], now];
  const points = 600;

  const seriesQueries = useQueries({
    queries: selected.map((id) => ({
      queryKey: keys.series(id, from, to, points),
      queryFn: () => api.get<TelemetrySeries>(`/telemetry/${encodeURIComponent(id)}/series`, { from, to, maxPoints: points }),
      placeholderData: (prev: TelemetrySeries | undefined) => prev,
    })),
  });

  const toggle = (id: string) => {
    const p = new URLSearchParams(params);
    const current = selected;
    p.delete('channel');
    const next = current.includes(id) ? current.filter((c) => c !== id) : [...current, id].slice(-6);
    next.forEach((c) => p.append('channel', c));
    setParams(p, { replace: true });
  };

  const loaded = seriesQueries.map((q) => q.data).filter((x): x is TelemetrySeries => !!x);
  const byUnit = new Map<string, { series: TelemetrySeries; color: string }[]>();
  loaded.forEach((s) => {
    const unit = s.channel.valueType === 'boolean' ? 'state (0 = false, 1 = true)' : s.channel.unit || '';
    const color = COLORS[selected.indexOf(s.channelId) % COLORS.length]!;
    byUnit.set(unit, [...(byUnit.get(unit) ?? []), { series: s, color }]);
  });

  return (
    <div className="grid-main-side" style={{ gridTemplateColumns: '300px minmax(0, 1fr)' }}>
      <Panel title="Channels" subtitle="Select up to six" flush>
        <QueryState
          query={channels}
          isEmpty={(d) => d.channels.length === 0}
          empty={<EmptyState compact title="No telemetry available">No data source delivers observations for this asset.</EmptyState>}
        >
          {(d) => (
            <ul className="vts-list" style={{ padding: '0 var(--s-3)' }}>
              {d.channels.map((c: TelemetryChannel) => (
                <li key={c.id}>
                  <label className="row" style={{ alignItems: 'flex-start', cursor: 'pointer' }}>
                    <input type="checkbox" checked={selected.includes(c.id)} onChange={() => toggle(c.id)} style={{ marginTop: 3 }} />
                    <span className="grow stack-sm" style={{ gap: 1 }}>
                      <span className="row-between">
                        <span className="small strong truncate">{c.presentation?.label ?? c.name}</span>
                        {c.freshness && <FreshnessBadge freshness={c.freshness} />}
                      </span>
                      <span className="small num">
                        {c.latest ? formatValue(c.latest.value, c.presentation?.precision, displayUnit(c.unit)) : 'no data'}
                      </span>
                      <span className="xsmall subtle truncate" title={c.source}>
                        {c.ontologySymbol ? <>ontology: <span className="mono">{c.ontologySymbol}</span> · </> : null}
                        {c.latest ? <TimeStamp value={c.latest.observedAt} relative /> : 'never observed'}
                      </span>
                    </span>
                  </label>
                </li>
              ))}
            </ul>
          )}
        </QueryState>
      </Panel>
      <div className="stack">
        <Panel
          title="Trends"
          subtitle={
            <>
              <TimeStamp value={from} kind="observed" /> – <TimeStamp value={to} /> ({Intl.DateTimeFormat().resolvedOptions().timeZone})
            </>
          }
          actions={
            <>
              <Segmented
                label="Time range"
                value={range}
                onChange={(r) => {
                  setRange(r);
                  setNow(Date.now());
                  setFollowing(true);
                }}
                options={[
                  { id: 'live', label: 'Live' },
                  { id: '15m', label: '15 min' },
                  { id: '1h', label: '1 h' },
                  { id: '6h', label: '6 h' },
                  { id: '24h', label: '24 h' },
                  { id: '7d', label: '7 d' },
                  { id: 'custom', label: 'Custom' },
                ]}
              />
              {range !== 'custom' && (
                <Button size="sm" icon={following ? <Pause size={13} /> : <Play size={13} />} onClick={() => { setFollowing((f) => !f); setNow(Date.now()); }}>
                  {following ? 'Pause' : 'Jump to now'}
                </Button>
              )}
              <Button size="sm" icon={<Download size={13} />} disabled={loaded.length === 0}
                onClick={() => downloadText(`telemetry-${assetId}.csv`, 'text/csv', exportCsv(loaded))}>CSV</Button>
              <Button size="sm" icon={<Download size={13} />} disabled={loaded.length === 0}
                onClick={() => downloadText(`telemetry-${assetId}.json`, 'application/json', JSON.stringify(loaded, null, 2))}>JSON</Button>
            </>
          }
        >
          <div className="stack">
            {range === 'custom' && (
              <div className="row-wrap">
                <label className="vts-field">
                  <span className="vts-label">From</span>
                  <input className="vts-input" type="datetime-local" value={customFrom} onChange={(e) => setCustomFrom(e.target.value)} />
                </label>
                <label className="vts-field">
                  <span className="vts-label">To</span>
                  <input className="vts-input" type="datetime-local" value={customTo} onChange={(e) => setCustomTo(e.target.value)} />
                </label>
              </div>
            )}
            {!following && range !== 'custom' && (
              <Callout tone="warning">Not following live data: the window is frozen at <TimeStamp value={to} />.</Callout>
            )}
            {selected.length === 0 && <EmptyState compact title="Select a channel to plot" />}
            {seriesQueries.some((q) => q.isError) && (
              <Callout tone="critical">Some series could not be loaded: {(seriesQueries.find((q) => q.isError)?.error as Error)?.message}</Callout>
            )}
            {[...byUnit.entries()].map(([unit, items]) => (
              <TimeSeriesChart key={unit} items={items} unit={unit} title={`${items.map((i) => i.series.channel.presentation?.label ?? i.series.channel.name).join(', ')}${unit ? ` (${displayUnit(unit)})` : ''}`} />
            ))}
            {byUnit.size > 1 && <p className="xsmall subtle">Channels with different units are drawn on separate charts; nothing is rescaled.</p>}
          </div>
        </Panel>
      </div>
    </div>
  );
}
