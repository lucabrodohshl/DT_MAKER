/**
 * Engineering audit trail (artefact and lifecycle operations). Separate from the
 * runtime's execution ledger. Hash-chained; verification is run by the server.
 */
import { useMutation } from '@tanstack/react-query';
import type { ColumnDef } from '@tanstack/react-table';
import { ShieldCheck } from 'lucide-react';
import { useState } from 'react';
import { useSearchParams } from 'react-router-dom';
import { useAudit, verifyAudit } from '@/api/queries';
import type { AuditRecord } from '@/api/types';
import { Button, Callout, DataTable, Drawer, HashChip, KeyValue, PageHeader, Panel, QueryState, TimeStamp } from '@/design';
import { Crumbs } from '@/features/common/links';
import { AuditOperation } from './auditFormat';

export function AuditVerifyPanel() {
  const v = useMutation({ mutationFn: verifyAudit });
  return (
    <Panel title="Chain verification">
      <div className="stack">
        <p className="small muted">
          Each record's hash covers its content and the previous record's hash. Verification recomputes the whole chain on
          the server.
        </p>
        <Button variant="primary" icon={<ShieldCheck size={14} />} loading={v.isPending} onClick={() => v.mutate()}>
          Verify engineering audit chain
        </Button>
        {v.error && <Callout tone="critical" title="Verification could not run">{(v.error as Error).message}</Callout>}
        {v.data &&
          (v.data.valid ? (
            <Callout tone="ok" title="AUDIT CHAIN VALID">
              {v.data.records} records verified; head <HashChip value={v.data.headHash} /> (<TimeStamp value={v.data.verifiedAt} />).
            </Callout>
          ) : (
            <Callout tone="critical" title="AUDIT CHAIN INVALID">
              First invalid record #{v.data.firstInvalidSeq}: {v.data.reason}
            </Callout>
          ))}
      </div>
    </Panel>
  );
}

export function AuditTable({ subject, operation }: { subject?: string; operation?: string }) {
  const [offset, setOffset] = useState(0);
  const [selected, setSelected] = useState<AuditRecord | null>(null);
  const q = useAudit({ subject, operation, limit: 100, offset });
  const columns: ColumnDef<AuditRecord, unknown>[] = [
    { header: '#', accessorKey: 'seq', meta: { align: 'right', width: 56 } },
    { header: 'When', accessorKey: 'at', cell: (c) => <TimeStamp value={String(c.getValue())} /> },
    { header: 'Operation', id: 'op', accessorFn: (r) => r.operation, cell: (c) => <AuditOperation record={c.row.original} /> },
    { header: 'Hash', id: 'hash', enableSorting: false, cell: (c) => <HashChip value={c.row.original.hash} length={10} /> },
  ];
  return (
    <>
      <QueryState query={q}>
        {(d) => (
          <div className="stack-sm">
            <DataTable caption="Engineering audit records" data={d.items} columns={columns} getRowId={(r) => String(r.seq)} onRowClick={setSelected} />
            <div className="row-between small" style={{ padding: '0 var(--s-4) var(--s-3)' }}>
              <span className="muted">
                {d.total === 0 ? 'No records' : `${offset + 1}–${Math.min(offset + d.items.length, d.total)} of ${d.total}`}
              </span>
              <span className="row">
                <Button size="sm" disabled={offset === 0} onClick={() => setOffset((o) => Math.max(0, o - 100))}>Newer</Button>
                <Button size="sm" disabled={offset + 100 >= d.total} onClick={() => setOffset((o) => o + 100)}>Older</Button>
              </span>
            </div>
          </div>
        )}
      </QueryState>
      <Drawer open={!!selected} onOpenChange={(o) => !o && setSelected(null)} title={selected ? `Audit record #${selected.seq}` : ''} subtitle={selected?.operation}>
        {selected && (
          <div className="stack">
            <KeyValue
              compact
              items={[
                ['Time', <TimeStamp key="t" value={selected.at} />],
                ['Actor', selected.actor],
                ['Operation', selected.operation],
                ['Subject', <span key="s" className="mono">{selected.subject}</span>],
                ['Outcome', selected.outcome],
                ['Previous hash', <HashChip key="p" value={selected.prevHash} />],
                ['Hash', <HashChip key="h" value={selected.hash} />],
              ]}
            />
            <pre className="vts-code">{JSON.stringify(selected.details, null, 2)}</pre>
          </div>
        )}
      </Drawer>
    </>
  );
}

export default function EngineeringAuditPage() {
  const [params, setParams] = useSearchParams();
  const subject = params.get('subject') ?? '';
  const operation = params.get('operation') ?? '';
  const set = (k: string, v: string) => {
    const p = new URLSearchParams(params);
    if (v) p.set(k, v);
    else p.delete(k);
    setParams(p, { replace: true });
  };
  return (
    <div className="vts-page">
      <PageHeader
        eyebrow={<Crumbs items={[{ label: 'Audit', to: '/audit' }, { label: 'Engineering audit' }]} />}
        title="Engineering audit trail"
        meta={<span>Who changed which artefact, which checks ran, what was released and deployed</span>}
      />
      <div className="grid-main-side">
        <Panel
          title="Records"
          actions={
            <>
              <input className="vts-input" placeholder="Subject (e.g. process-pump, CHG-0002)" value={subject} onChange={(e) => set('subject', e.target.value)} aria-label="Filter by subject" />
              <select className="vts-select" value={operation} onChange={(e) => set('operation', e.target.value)} aria-label="Filter by operation">
                <option value="">All operations</option>
                <option value="artifact.">Artefacts</option>
                <option value="refinement.">Refinement</option>
                <option value="alignment.">Alignment</option>
                <option value="package.">Packages</option>
                <option value="change.">Changes</option>
                <option value="deployment.">Deployments</option>
              </select>
            </>
          }
          flush
        >
          <AuditTable subject={subject || undefined} operation={operation || undefined} />
        </Panel>
        <AuditVerifyPanel />
      </div>
    </div>
  );
}
