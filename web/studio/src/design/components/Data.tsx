/**
 * Small data-presentation primitives: hashes, timestamps (with explicit time kind),
 * key/value lists, metrics and formulas.
 */
import { Check, Copy } from 'lucide-react';
import { useState, type ReactNode } from 'react';
import clsx from 'clsx';
import { useDisclosure } from '@/app/disclosure';

/** Shortened content hash with copy-to-clipboard; the full value is in the accessible label/title. */
export function HashChip({ value, label, length = 12 }: { value: string | null | undefined; label?: string; length?: number }) {
  const [copied, setCopied] = useState(false);
  const { engineering } = useDisclosure();
  if (!value) return <span className="subtle">—</span>;
  // Operator view: hashes are secondary, so show a short muted fingerprint (full value on hover).
  if (!engineering) return <span className="xsmall subtle mono" title={`${label ?? 'hash'} ${value}`}>{value.slice(0, 6)}</span>;
  const short = value.length > length ? `${value.slice(0, length)}…` : value;
  return (
    <span className="vts-hash" title={value}>
      <span aria-label={`${label ?? 'hash'} ${value}`}>{short}</span>
      <button
        type="button"
        onClick={() => {
          void navigator.clipboard?.writeText(value).then(() => {
            setCopied(true);
            setTimeout(() => setCopied(false), 1200);
          });
        }}
        aria-label={`Copy ${label ?? 'hash'}`}
      >
        {copied ? <Check size={12} /> : <Copy size={12} />}
      </button>
    </span>
  );
}

/**
 * Which time a timestamp represents. Times of different kinds are never mixed
 * silently: the kind is shown as a prefix where ambiguity is possible.
 */
export type TimeKind = 'observed' | 'ingested' | 'evidence' | 'deployed' | 'published' | 'event' | 'display';

const KIND_LABEL: Record<TimeKind, string> = {
  observed: 'Observed',
  ingested: 'Ingested',
  evidence: 'Checked',
  deployed: 'Deployed',
  published: 'Published',
  event: 'At',
  display: '',
};

const fmtCache = new Map<string, Intl.DateTimeFormat>();
function formatter(withSeconds: boolean, withDate: boolean): Intl.DateTimeFormat {
  const key = `${withSeconds}-${withDate}`;
  let f = fmtCache.get(key);
  if (!f) {
    f = new Intl.DateTimeFormat(undefined, {
      ...(withDate ? { year: 'numeric', month: 'short', day: '2-digit' } : {}),
      hour: '2-digit',
      minute: '2-digit',
      ...(withSeconds ? { second: '2-digit' } : {}),
      hour12: false,
    });
    fmtCache.set(key, f);
  }
  return f;
}

export function formatTime(iso: string | number | null | undefined, opts: { seconds?: boolean; date?: boolean } = {}): string {
  if (iso === null || iso === undefined || iso === '') return '—';
  const d = typeof iso === 'number' ? new Date(iso) : new Date(iso);
  if (Number.isNaN(d.getTime())) return String(iso);
  return formatter(opts.seconds ?? true, opts.date ?? true).format(d);
}

export function formatRelative(iso: string | number | null | undefined, now = Date.now()): string {
  if (iso === null || iso === undefined || iso === '') return '—';
  const t = typeof iso === 'number' ? iso : new Date(iso).getTime();
  if (Number.isNaN(t)) return '—';
  const s = Math.round((now - t) / 1000);
  const abs = Math.abs(s);
  const suffix = s >= 0 ? 'ago' : 'from now';
  if (abs < 5) return 'just now';
  if (abs < 60) return `${abs} s ${suffix}`;
  if (abs < 3600) return `${Math.round(abs / 60)} min ${suffix}`;
  if (abs < 86400) return `${Math.round(abs / 3600)} h ${suffix}`;
  return `${Math.round(abs / 86400)} d ${suffix}`;
}

/** Wall-clock timestamp in the viewer's time zone; the UTC value is in the tooltip and `dateTime`. */
export function TimeStamp({
  value,
  kind = 'display',
  relative,
  showKind,
  className,
}: {
  value: string | number | null | undefined;
  kind?: TimeKind;
  relative?: boolean;
  showKind?: boolean;
  className?: string;
}) {
  if (value === null || value === undefined || value === '') return <span className="subtle">—</span>;
  const iso = typeof value === 'number' ? new Date(value).toISOString() : value;
  const tz = Intl.DateTimeFormat().resolvedOptions().timeZone;
  const prefix = showKind && KIND_LABEL[kind] ? `${KIND_LABEL[kind]} ` : '';
  return (
    <time dateTime={iso} title={`${KIND_LABEL[kind] || 'Time'} (UTC): ${iso} · shown in ${tz}`} className={clsx('num', className)}>
      {prefix}
      {relative ? formatRelative(iso) : formatTime(iso)}
    </time>
  );
}

/** Logical model time (never wall-clock). */
export function LogicalTimeText({ text, unit }: { text: string | undefined; unit?: string }) {
  if (text === undefined) return <span className="subtle">—</span>;
  return (
    <span className="num" title="Logical model time (not wall-clock)">
      t = {text}
      {unit ? ` ${unit}` : ''}
    </span>
  );
}

export function KeyValue({ items, compact }: { items: [ReactNode, ReactNode][]; compact?: boolean }) {
  return (
    <dl className={clsx('vts-kv', compact && 'vts-kv--compact')}>
      {items.map(([k, v], i) => (
        <div key={i} style={{ display: 'contents' }}>
          <dt>{k}</dt>
          <dd>{v}</dd>
        </div>
      ))}
    </dl>
  );
}

export function Metric({ label, value, unit, meta }: { label: string; value: ReactNode; unit?: string; meta?: ReactNode }) {
  return (
    <div className="vts-metric">
      <span className="vts-metric__label">{label}</span>
      <span className="vts-metric__value">
        {value}
        {unit && <small>{unit}</small>}
      </span>
      {meta && <span className="vts-metric__meta">{meta}</span>}
    </div>
  );
}

export function Formula({ children }: { children: string }) {
  return <code className="vts-formula">{children}</code>;
}

/** Format a numeric value with the channel's precision and unit. */
export function formatValue(value: number | string | null | undefined, precision?: number, unit?: string): string {
  if (value === null || value === undefined) return '—';
  if (typeof value === 'string') return value;
  const p = precision ?? (Math.abs(value) >= 100 ? 0 : 2);
  const s = value.toLocaleString(undefined, { minimumFractionDigits: p, maximumFractionDigits: p });
  return unit ? `${s} ${unit}` : s;
}

export function displayUnit(unit: string | undefined): string {
  if (!unit) return '';
  if (unit === 'degC') return '°C';
  return unit;
}
