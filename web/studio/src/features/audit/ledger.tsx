/**
 * Shared execution-ledger UI: record kinds, record detail, chain verification,
 * and resolution of a ledger's package hash to Studio's package record (and thus
 * to the exact historical ontology / interpretation / model versions).
 */
import { useMutation } from '@tanstack/react-query';
import { ShieldAlert, ShieldCheck } from 'lucide-react';
import { useMemo } from 'react';
import { usePackages } from '@/api/queries';
import type { PackageRecord } from '@/api/types';
import { runtimeApi } from '@/runtime/client';
import type { LedgerRecord, LedgerVerification } from '@/runtime/types';
import { Button, Callout, Drawer, HashChip, KeyValue, StatusBadge, TimeStamp } from '@/design';
import { PackageLink, RefLink } from '@/features/common/links';

export const KIND_LABEL: Record<string, string> = {
  genesis: 'Session start',
  step: 'Transition',
  delay: 'Time passage',
  reject: 'Refused input',
  alarm: 'Alarm',
  context: 'Context',
  end: 'Session end',
};

export function kindTone(kind: string): 'ok' | 'info' | 'critical' | 'warning' | 'neutral' | 'formal' {
  return kind === 'step' ? 'info' : kind === 'reject' ? 'critical' : kind === 'alarm' ? 'warning' : kind === 'context' ? 'formal' : 'neutral';
}

/** One-line summary of a ledger record. */
export function recordSummary(r: LedgerRecord): string {
  const b = r.body;
  if (b.kind === 'step') {
    const br = b.outcome?.branches?.[0];
    return br ? `${br.label}: ${br.source} → ${br.target}` : b.input?.name ?? 'step';
  }
  if (b.kind === 'reject') return `${b.input?.name ?? 'input'} refused: ${b.outcome?.error?.message ?? (b as { error?: { message: string } }).error?.message ?? ''}`;
  if (b.kind === 'context') return `${b.topic ?? 'context'}`;
  if (b.kind === 'delay') return `time passes to ${b.time_after ?? '?'} ticks`;
  if (b.kind === 'alarm') return String((b as { alarm?: unknown }).alarm ?? 'alarm');
  return KIND_LABEL[b.kind] ?? b.kind;
}

export function useStudioPackageForHash(twinId: string | undefined, hash: string | undefined): PackageRecord | null {
  const pkgs = usePackages(twinId);
  return useMemo(() => pkgs.data?.find((p) => p.packageHash === hash) ?? null, [pkgs.data, hash]);
}

export function useVerifyLedger(twinId: string) {
  return useMutation({
    mutationFn: (session: string) => runtimeApi(twinId).post<LedgerVerification>('/runtime/ledger/verify', { session }),
  });
}

/** Chain verification result: LEDGER VALID only after a successful verification. */
export function VerificationResult({ result, error }: { result: LedgerVerification | undefined; error: unknown }) {
  if (error) return <Callout tone="critical" title="Verification could not run">{(error as Error).message}</Callout>;
  if (!result) return <StatusBadge tone="neutral" label="Not verified in this view" />;
  if (result.valid) {
    return (
      <Callout tone="ok" title="LEDGER VALID">
        {result.records} records; hash chain intact up to record #{result.head_seq} (head <HashChip value={result.head_hash} />).
        {result.has_end_record ? ' The session was closed in an orderly way.' : ' The session is still open (no end record yet).'}
      </Callout>
    );
  }
  const first = result.issues[0];
  return (
    <Callout tone="critical" title="LEDGER INVALID">
      {first ? (
        <>
          First invalid record at line {first.line}: {first.message} <span className="mono xsmall">({first.code})</span>
        </>
      ) : (
        'The chain did not verify.'
      )}
      {result.issues.length > 1 && <div className="xsmall">{result.issues.length} issue(s) in total.</div>}
    </Callout>
  );
}

export function VerifyButton({ twinId, session, onResult }: { twinId: string; session: string; onResult?: (r: LedgerVerification) => void }) {
  const v = useVerifyLedger(twinId);
  return (
    <div className="stack-sm">
      <Button
        variant="primary"
        icon={v.data?.valid === false ? <ShieldAlert size={14} /> : <ShieldCheck size={14} />}
        loading={v.isPending}
        onClick={() => v.mutate(session, { onSuccess: onResult })}
      >
        Verify chain now
      </Button>
      {(v.data || v.error) && <VerificationResult result={v.data} error={v.error} />}
      {v.data && <span className="xsmall subtle">Verified <TimeStamp value={v.submittedAt} /> by the runtime's ledger verifier.</span>}
    </div>
  );
}

export function RecordDrawer({
  record,
  open,
  onOpenChange,
  studioPackage,
}: {
  record: LedgerRecord | null;
  open: boolean;
  onOpenChange: (o: boolean) => void;
  studioPackage: PackageRecord | null;
}) {
  const b = record?.body;
  const binding = (role: string) => studioPackage?.bindings.find((x) => x.role === role);
  return (
    <Drawer open={open} onOpenChange={onOpenChange} title={record ? `Ledger record #${record.seq}` : 'Ledger record'} subtitle={b ? KIND_LABEL[b.kind] ?? b.kind : undefined}>
      {record && b && (
        <div className="stack">
          <KeyValue
            compact
            items={[
              ['Sequence', String(b.seq)],
              ['Execution', <span key="s" className="mono small">{b.session}</span>],
              ['Kind', <StatusBadge key="k" tone={kindTone(b.kind)} label={KIND_LABEL[b.kind] ?? b.kind} />],
              ...(b.topic ? [['Topic', b.topic] as [string, React.ReactNode]] : []),
              ['Logical time before', b.time_before !== undefined ? `${b.time_before} ticks` : '—'],
              ['Logical time after', b.time_after !== undefined ? `${b.time_after} ticks` : '—'],
              ['Source state', <span key="ss" className="mono small">{b.state_before?.map((s) => s.location).join(', ') ?? '—'}</span>],
              ['Event', <span key="e" className="mono small">{b.input ? `${b.input.name} (${b.input.kind}, from ${b.input.source})` : '—'}</span>],
              ['Transition', <span key="t" className="mono small">{b.outcome?.branches?.map((x) => x.transition).join(', ') ?? '—'}</span>],
              ['Destination state', <span key="d" className="mono small">{b.state_after?.map((s) => s.location).join(', ') ?? '—'}</span>],
              ['Propositions after', <span key="p" className="mono small">{b.propositions?.join(', ') ?? '—'}</span>],
              ['Input digest', <HashChip key="id" value={b.input_digest} />],
              ['Package hash', <HashChip key="ph" value={b.package?.hash} />],
              ['IR hash', <HashChip key="ir" value={b.package?.ir_sha256} />],
              ['Model', `${b.package?.model_id ?? '?'} ${b.package?.model_version ?? ''}`],
              ['Kernel', b.kernel_version],
              ['Previous record hash', <HashChip key="prev" value={b.prev_hash} />],
              ['Record hash', <HashChip key="h" value={record.hash} />],
            ]}
          />
          <div>
            <span className="vts-label">Artefacts of this execution (from the recorded package)</span>
            {studioPackage ? (
              <KeyValue
                compact
                items={[
                  ['Package', <PackageLink key="p" id={studioPackage.id} />],
                  ...['ontology', 'dt_interpretation', 'dt_model'].map((r): [string, React.ReactNode] => [
                    r === 'ontology' ? 'Ontology' : r === 'dt_interpretation' ? 'Interpretation' : 'DT model',
                    binding(r) ? <RefLink key={r} refId={binding(r)!.ref} /> : '—',
                  ]),
                  ['Ontology hash', <HashChip key="oh" value={binding('ontology')?.sha256} />],
                  ['Interpretation hash', <HashChip key="ih" value={binding('dt_interpretation')?.sha256} />],
                ]}
              />
            ) : (
              <p className="small muted">The package recorded in this ledger is not registered in Studio, so its artefact versions cannot be resolved here.</p>
            )}
          </div>
          <details>
            <summary className="small">Canonical record (JSON)</summary>
            <pre className="vts-code" style={{ maxHeight: 360 }}>{JSON.stringify(record, null, 2)}</pre>
          </details>
        </div>
      )}
    </Drawer>
  );
}
