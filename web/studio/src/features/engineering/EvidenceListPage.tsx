/** Engineering › Verification history (all evidence) and › Alignment (alignment evidence). */
import type { ColumnDef } from '@tanstack/react-table';
import { useState } from 'react';
import { useLocation, useNavigate } from 'react-router-dom';
import { useEvidenceList } from '@/api/queries';
import type { EvidenceRecord } from '@/api/types';
import { Button, DataTable, EmptyState, HashChip, OutcomeBadge, PageHeader, Panel, QueryState, TimeStamp, humanize } from '@/design';
import { Crumbs } from '@/features/common/links';

export default function EvidenceListPage() {
  const alignment = useLocation().pathname.endsWith('/alignment');
  const [kind, setKind] = useState(alignment ? 'alignment' : '');
  const [offset, setOffset] = useState(0);
  const q = useEvidenceList({ kind: kind || undefined, limit: 100, offset });
  const navigate = useNavigate();
  const columns: ColumnDef<EvidenceRecord, unknown>[] = [
    { header: 'Evidence', accessorKey: 'id', cell: (c) => <span className="mono small strong">{String(c.getValue())}</span> },
    { header: 'Check', accessorKey: 'kind', cell: (c) => humanize(String(c.getValue())) },
    { header: 'Result', id: 'r', accessorFn: (e) => e.outcome, cell: (c) => <OutcomeBadge outcome={c.row.original.outcome} verdict={c.row.original.verdict} /> },
    { header: 'Artefacts checked', id: 'in', enableSorting: false, cell: (c) => <span className="mono xsmall">{c.row.original.inputs.map((i) => i.ref).filter((x, i, a) => a.indexOf(x) === i).join(', ')}</span> },
    { header: 'When', accessorKey: 'createdAt', cell: (c) => <TimeStamp value={String(c.getValue())} /> },
    { header: 'By', accessorKey: 'createdBy' },
    { header: 'Evidence hash', id: 'h', enableSorting: false, cell: (c) => <HashChip value={c.row.original.evidenceSha256} length={8} /> },
  ];
  return (
    <div className="vts-page">
      <PageHeader
        eyebrow={<Crumbs items={[{ label: 'Studio', to: '/studio' }, { label: alignment ? 'Alignment' : 'Verification history' }]} />}
        title={alignment ? 'Semantic alignment evidence' : 'Verification history'}
        meta={<span>Every formal check is stored immutably with its exact inputs; newer runs never overwrite older evidence</span>}
      />
      <Panel
        title="Evidence"
        actions={
          <select className="vts-select" value={kind} onChange={(e) => { setKind(e.target.value); setOffset(0); }} aria-label="Check kind">
            <option value="">All checks</option>
            <option value="validation">Validation</option>
            <option value="refinement">Refinement</option>
            <option value="alignment">Alignment</option>
            <option value="compilation">Compilation</option>
            <option value="package">Package</option>
          </select>
        }
        flush
      >
        <QueryState query={q}>
          {(d) => (
            <div className="stack-sm">
              <DataTable
                caption="Evidence records"
                data={d.items}
                columns={columns}
                getRowId={(e) => e.id}
                onRowClick={(e) => navigate(e.kind === 'refinement' ? `/studio/refinement/${e.id}` : `/studio/verification/${e.id}`)}
                empty={<EmptyState compact title="No evidence of this kind" />}
              />
              <div className="row-between small" style={{ padding: '0 var(--s-4) var(--s-3)' }}>
                <span className="muted">{d.total} record(s)</span>
                <span className="row">
                  <Button size="sm" disabled={offset === 0} onClick={() => setOffset((o) => Math.max(0, o - 100))}>Newer</Button>
                  <Button size="sm" disabled={offset + 100 >= d.total} onClick={() => setOffset((o) => o + 100)}>Older</Button>
                </span>
              </div>
            </div>
          )}
        </QueryState>
      </Panel>
    </div>
  );
}
