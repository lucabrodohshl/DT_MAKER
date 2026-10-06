/**
 * Time-series chart (uPlot) for one unit group.
 *
 * Rules (product spec §7, §32):
 *  - one y-axis per chart; channels with different units are never drawn together;
 *  - downsampled data is shown as a min–max band with the mean line;
 *  - missing data is a gap (no interpolation across more than 3 expected periods);
 *  - samples with quality "bad"/"uncertain" are marked;
 *  - axes are labelled with units; the y-range is not silently truncated (it
 *    includes all values in range); drag to zoom, double-click / button to reset;
 *  - an accessible table alternative is always available.
 */
import { useEffect, useMemo, useRef, useState } from 'react';
import uPlot, { type AlignedData, type Options } from 'uplot';
import 'uplot/dist/uPlot.min.css';
import type { TelemetrySeries } from '@/api/types';
import { Button, displayUnit, formatTime, formatValue } from '@/design';

export interface ChartSeries {
  series: TelemetrySeries;
  color: string;
}

function cssVar(name: string): string {
  if (typeof window === 'undefined') return '#888';
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim() || '#888';
}

/** Merge per-channel series onto one shared x-axis (seconds), inserting gaps. */
export function buildAligned(input: ChartSeries[]): { data: AlignedData; labels: string[]; banded: boolean[]; badMarkers: number } {
  const xs = new Set<number>();
  const perChannel = input.map(({ series }) => {
    const gapMs = Math.max(3 * (series.channel.expectedPeriodMs || 1000), series.bucketMs ? 2.5 * series.bucketMs : 0);
    const points: { t: number; v: number | null; min?: number; max?: number; bad?: boolean }[] = [];
    if (series.downsampled && series.buckets) {
      for (const b of series.buckets) points.push({ t: (b.startMs + b.endMs) / 2, v: b.avg, min: b.min, max: b.max, bad: b.bad > 0 });
    } else if (series.samples) {
      for (const s of series.samples) {
        const v = typeof s.value === 'number' ? s.value : s.value === 'true' ? 1 : s.value === 'false' ? 0 : null;
        points.push({ t: s.observedMs, v, bad: s.quality !== 'good' });
      }
    }
    // Insert explicit nulls where consecutive points are further apart than the gap threshold.
    const withGaps: typeof points = [];
    for (let i = 0; i < points.length; i++) {
      const p = points[i]!;
      const prev = withGaps[withGaps.length - 1];
      if (prev && prev.v !== null && p.t - prev.t > gapMs) withGaps.push({ t: prev.t + 1, v: null });
      withGaps.push(p);
    }
    withGaps.forEach((p) => xs.add(p.t));
    return { points: withGaps, banded: !!series.downsampled };
  });
  const x = [...xs].sort((a, b) => a - b);
  const index = new Map(x.map((t, i) => [t, i]));
  const data: (number | null)[][] = [x.map((t) => t / 1000)];
  const labels: string[] = [];
  const banded: boolean[] = [];
  let badMarkers = 0;
  input.forEach(({ series }, k) => {
    const label = series.channel.presentation?.label ?? series.channel.name;
    const ch = perChannel[k]!;
    const mean = new Array<number | null>(x.length).fill(null);
    const lo = new Array<number | null>(x.length).fill(null);
    const hi = new Array<number | null>(x.length).fill(null);
    const bad = new Array<number | null>(x.length).fill(null);
    for (const p of ch.points) {
      const i = index.get(p.t)!;
      mean[i] = p.v;
      if (p.min !== undefined) lo[i] = p.min;
      if (p.max !== undefined) hi[i] = p.max;
      if (p.bad && p.v !== null) {
        bad[i] = p.v;
        badMarkers++;
      }
    }
    data.push(mean);
    labels.push(label);
    banded.push(ch.banded);
    if (ch.banded) {
      data.push(lo, hi);
      labels.push(`${label} (min)`, `${label} (max)`);
      banded.push(false, false);
    }
    data.push(bad);
    labels.push(`${label} (quality flag)`);
    banded.push(false);
  });
  return { data: data as AlignedData, labels, banded, badMarkers };
}

export function TimeSeriesChart({ items, unit, height = 260, title }: { items: ChartSeries[]; unit: string; height?: number; title: string }) {
  const host = useRef<HTMLDivElement>(null);
  const plot = useRef<uPlot | null>(null);
  const [showTable, setShowTable] = useState(false);
  const aligned = useMemo(() => buildAligned(items), [items]);

  useEffect(() => {
    if (!host.current) return;
    const el = host.current;
    const axisColor = cssVar('--text-muted');
    const grid = cssVar('--divider');
    const series: Options['series'] = [{ label: 'Time', value: (_u, v) => (v == null ? '—' : formatTime(v * 1000)) }];
    let k = 0;
    items.forEach((it) => {
      const color = cssVar(it.color);
      const precision = it.series.channel.presentation?.precision;
      const fmt = (_u: uPlot, v: number | null) => (v == null ? '—' : formatValue(v, precision, displayUnit(unit)));
      series.push({ label: aligned.labels[k], stroke: color, width: 1.6, value: fmt, spanGaps: false, points: { show: false } });
      k++;
      if (aligned.banded[k - 1]) {
        series.push({ label: aligned.labels[k], stroke: color, width: 0, value: fmt, spanGaps: false, points: { show: false } });
        series.push({ label: aligned.labels[k + 1], stroke: color, width: 0, value: fmt, spanGaps: false, points: { show: false } });
        k += 2;
      }
      series.push({
        label: aligned.labels[k],
        stroke: cssVar('--crit'),
        width: 0,
        value: (_u, v) => (v == null ? '' : 'flagged'),
        points: { show: true, size: 6, fill: cssVar('--crit'), stroke: cssVar('--crit') },
      });
      k++;
    });
    const bands: Options['bands'] = [];
    let idx = 1;
    items.forEach((_it, i) => {
      const color = cssVar(items[i]!.color);
      if (aligned.banded[idx - 1]) {
        bands.push({ series: [idx + 2, idx + 1], fill: `${color}26` });
        idx += 4;
      } else {
        idx += 2;
      }
    });
    const opts: Options = {
      width: el.clientWidth || 600,
      height,
      series,
      bands,
      scales: { x: { time: true } },
      axes: [
        { stroke: axisColor, grid: { stroke: grid }, ticks: { stroke: grid } },
        { stroke: axisColor, grid: { stroke: grid }, ticks: { stroke: grid }, label: displayUnit(unit) || 'value', labelSize: 18, size: 64 },
      ],
      legend: { show: true, live: true },
      cursor: { drag: { x: true, y: false }, points: { size: 6 } },
    };
    plot.current?.destroy();
    plot.current = new uPlot(opts, aligned.data, el);
    const ro = new ResizeObserver(() => plot.current?.setSize({ width: el.clientWidth, height }));
    ro.observe(el);
    return () => {
      ro.disconnect();
      plot.current?.destroy();
      plot.current = null;
    };
  }, [aligned, items, unit, height]);

  const resetZoom = () => {
    const u = plot.current;
    if (!u) return;
    const xs = u.data[0];
    if (xs.length) u.setScale('x', { min: xs[0]!, max: xs[xs.length - 1]! });
  };

  const totals = items.map((i) => `${i.series.channel.presentation?.label ?? i.series.channel.name}: ${i.series.totalSamples} samples${i.series.downsampled ? ' (min/mean/max per interval)' : ''}`);

  return (
    <figure className="stack-sm" style={{ margin: 0 }}>
      <figcaption className="row-between">
        <span className="small strong">{title}</span>
        <span className="row">
          {aligned.badMarkers > 0 && <span className="xsmall" style={{ color: 'var(--crit)' }}>● {aligned.badMarkers} quality-flagged</span>}
          <Button size="sm" variant="ghost" onClick={resetZoom}>Reset zoom</Button>
          <Button size="sm" variant="ghost" onClick={() => setShowTable((s) => !s)} aria-expanded={showTable}>
            {showTable ? 'Hide table' : 'Show as table'}
          </Button>
        </span>
      </figcaption>
      <div ref={host} role="img" aria-label={`${title}. ${totals.join('; ')}. Drag to zoom.`} onDoubleClick={resetZoom} />
      <p className="xsmall subtle">{totals.join(' · ')}. Gaps mark missing data; drag horizontally to zoom.</p>
      {showTable && <SeriesTable items={items} unit={unit} />}
    </figure>
  );
}

function SeriesTable({ items, unit }: { items: ChartSeries[]; unit: string }) {
  const rows = items.flatMap(({ series }) => {
    const label = series.channel.presentation?.label ?? series.channel.name;
    const precision = series.channel.presentation?.precision;
    if (series.downsampled && series.buckets) {
      return series.buckets.slice(-500).map((b) => ({
        key: `${series.channelId}-${b.startMs}`,
        label,
        time: formatTime(b.startMs),
        value: `${formatValue(b.avg, precision)} (min ${formatValue(b.min, precision)}, max ${formatValue(b.max, precision)})`,
        quality: b.bad ? `${b.bad} bad` : b.uncertain ? `${b.uncertain} uncertain` : 'good',
      }));
    }
    return (series.samples ?? []).slice(-500).map((s) => ({
      key: `${series.channelId}-${s.observedMs}`,
      label,
      time: formatTime(s.observedAt),
      value: typeof s.value === 'number' ? formatValue(s.value, precision) : String(s.value),
      quality: s.quality,
    }));
  });
  return (
    <div className="vts-table-wrap" style={{ maxHeight: 280 }}>
      <table className="vts-table">
        <caption className="sr-only">Values ({displayUnit(unit)}), latest 500 per channel</caption>
        <thead>
          <tr>
            <th scope="col">Channel</th>
            <th scope="col">Observed</th>
            <th scope="col" className="num">Value ({displayUnit(unit) || '—'})</th>
            <th scope="col">Quality</th>
          </tr>
        </thead>
        <tbody>
          {rows.map((r) => (
            <tr key={r.key}>
              <td>{r.label}</td>
              <td className="num">{r.time}</td>
              <td className="num">{r.value}</td>
              <td>{r.quality}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}
